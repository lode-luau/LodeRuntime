// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "CdefParser.hpp"
#include "FfiTypes.hpp"

#include <cctype>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <string_view>
#include <unordered_map>

namespace lodeffi
{
namespace
{

[[noreturn]] void Fail(size_t line, const std::string& message)
{
    throw std::runtime_error("ffi.cdef: line " + std::to_string(line) + ": " + message);
}

struct Token
{
    std::string text;
    size_t line = 1;
};

// Tokenizer: identifiers/numbers, single-char punctuation, C/C++ comments.
// Line tracking is kept so parse errors point at the offending declaration.
std::vector<Token> Tokenize(const std::string& src)
{
    std::vector<Token> out;
    size_t i = 0;
    size_t line = 1;
    const size_t n = src.size();
    while (i < n)
    {
        char c = src[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        // Comments.
        if (c == '/' && i + 1 < n && (src[i + 1] == '/' || src[i + 1] == '*'))
        {
            if (src[i + 1] == '/')
            {
                while (i < n && src[i] != '\n') ++i;
            }
            else
            {
                i += 2;
                while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/'))
                {
                    if (src[i] == '\n') ++line;
                    ++i;
                }
                if (i + 1 >= n) Fail(line, "unterminated block comment");
                i += 2;
            }
            continue;
        }

        // Identifiers / numbers.
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        {
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) ++i;
            out.push_back({ src.substr(start, i - start), line });
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '.')) ++i;
            out.push_back({ src.substr(start, i - start), line });
            continue;
        }

        // Punctuation of interest.
        if (c == '(' || c == ')' || c == ',' || c == ';' || c == '*' || c == '[' || c == ']' ||
            c == '{' || c == '}' || c == '=' || c == '+' || c == '-' || c == '<' || c == '>' ||
            c == '|' || c == '&' || c == '~')
        {
            out.push_back({ std::string(1, c), line });
            ++i;
            continue;
        }

        Fail(line, std::string("unexpected character '") + c + "'");
    }
    return out;
}

bool IsBaseTypeWord(const std::string& w)
{
    static const std::map<std::string, int> kWords = {
        { "void", 1 }, { "bool", 1 }, { "_Bool", 1 },
        { "char", 1 }, { "short", 1 }, { "int", 1 }, { "long", 1 },
        { "float", 1 }, { "double", 1 }, { "signed", 1 }, { "unsigned", 1 },
        { "const", 1 }, { "volatile", 1 }, { "restrict", 1 },
        { "int8_t", 1 }, { "int16_t", 1 }, { "int32_t", 1 }, { "int64_t", 1 },
        { "uint8_t", 1 }, { "uint16_t", 1 }, { "uint32_t", 1 }, { "uint64_t", 1 },
        { "intptr_t", 1 }, { "uintptr_t", 1 }, { "size_t", 1 }, { "ssize_t", 1 },
        { "ptrdiff_t", 1 }, { "wchar_t", 1 },
        { "__int32", 1 }, { "__int64", 1 },
    };
    return kWords.count(w) != 0;
}

bool Contains(const std::string& value, const char* needle)
{
    return value.find(needle) != std::string::npos;
}

bool IsCallingConvention(const std::string& token)
{
    return token == "__cdecl" || token == "__stdcall" || token == "__fastcall" ||
           token == "__vectorcall" || token == "WINAPI" || token == "APIENTRY" ||
           token == "CALLBACK";
}

constexpr bool kDefaultAbiMatchesWindowsConvention =
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)
    true;
#else
    false;
#endif

struct Cursor
{
    const std::vector<Token>* toks;
    const std::unordered_map<std::string, ArgClass>* aliases = nullptr;
    const std::unordered_set<std::string>* opaqueAggregateAliases = nullptr;
    const std::unordered_set<std::string>* definedAggregateAliases = nullptr;
    size_t pos = 0;

    [[nodiscard]] bool Eof() const { return pos >= toks->size(); }
    [[nodiscard]] size_t LastLine() const
    {
        return pos > 0 ? (*toks)[pos - 1].line : 1;
    }
    [[nodiscard]] const Token& Peek() const { return (*toks)[pos]; }
    const Token& Next()
    {
        if (Eof()) Fail(LastLine(), "unexpected end of declarations");
        return (*toks)[pos++];
    }
};

void SkipEnumDefinition(Cursor& cur)
{
    // `enum Tag { A = 1, B = 2 };` affects no calling convention in this
    // subset. Consume it so a following function may use the tag/typedef.
    cur.Next(); // enum
    if (!cur.Eof() && cur.Peek().text != "{")
        cur.Next(); // optional enum tag
    if (cur.Eof() || cur.Peek().text != "{")
        return;

    size_t depth = 0;
    do
    {
        const Token token = cur.Next();
        if (token.text == "{") ++depth;
        if (token.text == "}") --depth;
    } while (depth != 0 && !cur.Eof());
    if (depth != 0)
        Fail(cur.LastLine(), "unterminated enum definition");
}

void SkipAggregateDefinition(Cursor& cur)
{
    const Token aggregate = cur.Next(); // struct or union
    if (!cur.Eof() && cur.Peek().text != "{")
        cur.Next(); // optional tag
    if (cur.Eof() || cur.Peek().text != "{")
        return;

    size_t depth = 0;
    do
    {
        const Token token = cur.Next();
        if (token.text == "{") ++depth;
        if (token.text == "}") --depth;
    } while (depth != 0 && !cur.Eof());
    if (depth != 0)
        Fail(cur.LastLine(), "unterminated " + aggregate.text + " definition");
}

// Parses a type: qualifiers/base words followed by zero or more '*'.
// Returns the resolved argument class plus whether the type is pointer-to-
// char-or-void (informational only in v1; all pointers marshal identically).
ArgClass ParseType(Cursor& cur, RetKind& retOut, bool allowVoid,
                   std::string* structName = nullptr)
{
    std::string base;
    unsigned stars = 0;
    bool sawVoid = false;
    bool sawFloat = false;
    bool sawDouble = false;

    // Qualifiers do not affect the ABI. Consume leading const so opaque
    // declarations such as `const struct FILE*` reach the struct branch.
    while (!cur.Eof() && (cur.Peek().text == "const" || cur.Peek().text == "volatile" ||
                          cur.Peek().text == "restrict"))
        cur.Next();

    if (!cur.Eof() && (cur.Peek().text == "struct" || cur.Peek().text == "union"))
    {
        const Token aggregate = cur.Next();
        if (cur.Eof() || cur.Peek().text == "*" || cur.Peek().text == "," ||
            cur.Peek().text == ")")
            Fail(cur.Eof() ? cur.LastLine() : cur.Peek().line,
                 "expected " + aggregate.text + " tag");
        cur.Next(); // opaque aggregate tag

        unsigned aggregateStars = 0;
        while (!cur.Eof() && cur.Peek().text == "*")
        {
            ++aggregateStars;
            cur.Next();
        }
        if (aggregateStars == 0)
            Fail(cur.Eof() ? cur.LastLine() : cur.Peek().line,
                 aggregate.text + " values by value are not supported; use a pointer");
        retOut = RetKind::Ptr;
        return ArgClass::Ptr;
    }

    if (!cur.Eof() && cur.Peek().text == "enum")
    {
        const Token enumToken = cur.Next();
        if (!cur.Eof() && cur.Peek().text != "*" && cur.Peek().text != "," &&
            cur.Peek().text != ")" && cur.Peek().text != "[")
            cur.Next(); // optional enum tag
        retOut = RetKind::I32;
        return ArgClass::I32;
    }

    // A typedef name is a complete type in this subset. Qualifiers preceding
    // it (for example `const DWORD`) do not alter the calling ABI.
    while (!cur.Eof() && (IsBaseTypeWord(cur.Peek().text) ||
                          (cur.aliases != nullptr && cur.aliases->contains(cur.Peek().text))))
    {
        if (cur.aliases != nullptr)
        {
            const auto alias = cur.aliases->find(cur.Peek().text);
            if (alias != cur.aliases->end())
            {
                const Token aliasName = cur.Next();
                while (!cur.Eof() && (cur.Peek().text == "const" || cur.Peek().text == "volatile" ||
                                      cur.Peek().text == "restrict"))
                    cur.Next();
                bool pointerToAlias = false;
                while (!cur.Eof() && cur.Peek().text == "*")
                {
                    pointerToAlias = true;
                    cur.Next();
                }
                if (cur.definedAggregateAliases != nullptr &&
                    cur.definedAggregateAliases->contains(aliasName.text) && !pointerToAlias)
                {
                    if (structName != nullptr)
                        *structName = aliasName.text;
                    return ArgClass::Struct;
                }
                if (cur.opaqueAggregateAliases != nullptr &&
                    cur.opaqueAggregateAliases->contains(aliasName.text) && !pointerToAlias)
                {
                    Fail(aliasName.line,
                         "struct or union values by value are not supported; use '" +
                         aliasName.text + "*'");
                }
                if (pointerToAlias)
                {
                    retOut = RetKind::Ptr;
                    return ArgClass::Ptr;
                }
                switch (alias->second)
                {
                    case ArgClass::Bool: retOut = RetKind::Bool; break;
                    case ArgClass::I8: retOut = RetKind::I8; break;
                    case ArgClass::U8: retOut = RetKind::U8; break;
                    case ArgClass::I16: retOut = RetKind::I16; break;
                    case ArgClass::U16: retOut = RetKind::U16; break;
                    case ArgClass::I32: retOut = RetKind::I32; break;
                    case ArgClass::U32: retOut = RetKind::U32; break;
                    case ArgClass::I64: retOut = RetKind::I64; break;
                    case ArgClass::U64: retOut = RetKind::U64; break;
                    case ArgClass::Ptr: retOut = RetKind::Ptr; break;
                    case ArgClass::F32: retOut = RetKind::F32; break;
                    case ArgClass::F64: retOut = RetKind::F64; break;
                }
                return alias->second;
            }
        }

        std::string w = cur.Next().text;
        if (w == "void") sawVoid = true;
        if (w == "float") sawFloat = true;
        if (w == "double") sawDouble = true;
        if (!base.empty()) base += ' ';
        base += w;
    }
    while (!cur.Eof() && cur.Peek().text == "*")
    {
        ++stars;
        cur.Next();
    }

    if (base.empty()) Fail(cur.Eof() ? cur.LastLine() : cur.Peek().line, "expected a type");
    if (sawVoid && !allowVoid)
        Fail(cur.Eof() ? cur.LastLine() : cur.Peek().line, "'void' is only valid as the sole return type");

    if (stars > 0)
    {
        // Preserve pointer identity for libffi's ABI classification. char*/
        // void* accept Lua strings/buffers at call time; see ExtractIntArg.
        retOut = RetKind::Ptr;
        return ArgClass::Ptr;
    }
    if (sawFloat && !sawDouble) { retOut = RetKind::F32; return ArgClass::F32; }
    if (sawDouble) { retOut = RetKind::F64; return ArgClass::F64; }
    if (sawVoid) { retOut = RetKind::Void; return ArgClass::I32; } // unused

    const bool isUnsigned = Contains(base, "unsigned") ||
                            base == "uint8_t" || base == "uint16_t" ||
                            base == "uint32_t" || base == "uint64_t" ||
                            base == "uintptr_t" || base == "size_t";
    const bool isBool = base == "bool" || base == "_Bool";
    const bool isByte = base == "int8_t" || base == "uint8_t" ||
                        Contains(base, "char") || isBool;
    const bool isShort = base == "int16_t" || base == "uint16_t" ||
                         Contains(base, "short");
#if defined(_WIN32)
    const bool isWide = Contains(base, "64") || Contains(base, "long long") ||
                        base == "size_t" || base == "uintptr_t" || base == "ssize_t" ||
                        base == "ptrdiff_t";
#else
    const bool isWide = Contains(base, "64") || Contains(base, "long long") ||
                        Contains(base, "long") || base == "size_t" ||
                        base == "uintptr_t" || base == "ssize_t" || base == "ptrdiff_t";
#endif
    if (isBool)
    {
        retOut = RetKind::Bool;
        return ArgClass::Bool;
    }
    if (isByte)
    {
        retOut = isUnsigned ? RetKind::U8 : RetKind::I8;
        return isUnsigned ? ArgClass::U8 : ArgClass::I8;
    }
    if (isShort)
    {
        retOut = isUnsigned ? RetKind::U16 : RetKind::I16;
        return isUnsigned ? ArgClass::U16 : ArgClass::I16;
    }
    if (isWide)
    {
        retOut = isUnsigned ? RetKind::U64 : RetKind::I64;
        return isUnsigned ? ArgClass::U64 : ArgClass::I64;
    }
    retOut = isUnsigned ? RetKind::U32 : RetKind::I32;
    return isUnsigned ? ArgClass::U32 : ArgClass::I32;
}

StructLayout ParseAggregateLayout(Cursor& cur)
{
    const Token aggregate = cur.Next(); // struct or union
    if (!cur.Eof() && cur.Peek().text != "{")
        cur.Next(); // optional tag

    StructLayout layout;
    layout.isUnion = aggregate.text == "union";
    if (cur.Eof() || cur.Peek().text != "{")
        return layout; // opaque forward declaration

    cur.Next(); // {
    while (!cur.Eof() && cur.Peek().text != "}")
    {
        RetKind ignored = RetKind::Void;
        std::string fieldStructName;
        const ArgClass field = ParseType(cur, ignored, /*allowVoid=*/false, &fieldStructName);
        if (field == ArgClass::I32 && ignored == RetKind::Void)
            Fail(cur.LastLine(), "struct fields cannot have type void");
        if (cur.Eof() || cur.Peek().text == ";")
            Fail(cur.Eof() ? cur.LastLine() : cur.Peek().line, "expected struct field name");
        cur.Next(); // field name; layout offsets are positional in v1
        size_t count = 1;
        if (!cur.Eof() && cur.Peek().text == "[")
        {
            const size_t arrayLine = cur.Next().line;
            if (cur.Eof() || cur.Peek().text == "]")
                Fail(arrayLine, "struct array field requires a fixed size");
            const Token extent = cur.Next();
            if (extent.text.empty() || !std::isdigit(static_cast<unsigned char>(extent.text.front())))
                Fail(extent.line, "struct array size must be an integer literal");
            count = static_cast<size_t>(std::stoull(extent.text));
            if (count == 0) Fail(extent.line, "struct array size must not be zero");
            if (cur.Eof() || cur.Next().text != "]")
                Fail(extent.line, "expected ']' after struct array size");
        }
        if (cur.Eof() || cur.Next().text != ";")
            Fail(cur.LastLine(), "expected ';' after struct field");
        layout.fields.insert(layout.fields.end(), count, field);
        layout.fieldStructNames.insert(layout.fieldStructNames.end(), count, fieldStructName);
    }
    if (cur.Eof())
        Fail(cur.LastLine(), "unterminated struct definition");
    cur.Next(); // }
    return layout;
}

} // namespace

std::string Prototype::Describe() const
{
    auto clsName = [](ArgClass c) -> const char* {
        switch (c)
        {
            case ArgClass::Bool: return "bool";
            case ArgClass::I8: return "int8";
            case ArgClass::U8: return "uint8";
            case ArgClass::I16: return "int16";
            case ArgClass::U16: return "uint16";
            case ArgClass::I32: return "int32";
            case ArgClass::U32: return "uint32";
            case ArgClass::I64: return "int64";
            case ArgClass::U64: return "uint64";
            case ArgClass::Ptr: return "ptr";
            case ArgClass::F32: return "float";
            case ArgClass::F64: return "double";
            case ArgClass::Struct: return "struct";
        }
        return "?";
    };

    std::string s = name.empty() ? "(anonymous)" : name;
    s += "(";
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i != 0) s += ", ";
        s += clsName(args[i]);
    }
    s += ")";
    return s;
}

CdefParseResult ParseCdef(const std::string& source)
{
    std::vector<Token> tokens = Tokenize(source);
    std::unordered_map<std::string, ArgClass> aliases;
    std::unordered_set<std::string> opaqueAggregateAliases;
    std::unordered_set<std::string> definedAggregateAliases;
    Cursor cur{ &tokens, &aliases, &opaqueAggregateAliases, &definedAggregateAliases };
    std::vector<Prototype> protos;
    std::vector<StructLayout> structs;

    while (!cur.Eof())
    {
        if (cur.Peek().text == "typedef")
        {
            const Token keyword = cur.Next();
            if (!cur.Eof() && (cur.Peek().text == "struct" || cur.Peek().text == "union"))
            {
                StructLayout layout = ParseAggregateLayout(cur);
                if (cur.Eof() || cur.Peek().text == ";")
                    Fail(cur.Eof() ? cur.LastLine() : cur.Peek().line, "expected typedef name");
                const Token name = cur.Next();
                if (cur.Eof() || cur.Next().text != ";")
                    Fail(cur.LastLine(), "expected ';' after typedef '" + name.text + "'");
                // Defined layouts can be passed by value; forward-only
                // declarations remain pointer-only.
                aliases[name.text] = ArgClass::Ptr;
                opaqueAggregateAliases.insert(name.text);
                if (!layout.fields.empty())
                {
                    layout.name = name.text;
                    structs.push_back(std::move(layout));
                    if (!structs.back().isUnion)
                        definedAggregateAliases.insert(name.text);
                }
                continue;
            }
            if (!cur.Eof() && cur.Peek().text == "enum")
            {
                SkipEnumDefinition(cur);
                if (cur.Eof() || cur.Peek().text == ";")
                    Fail(cur.Eof() ? cur.LastLine() : cur.Peek().line, "expected typedef name");
                const Token name = cur.Next();
                if (cur.Eof() || cur.Next().text != ";")
                    Fail(cur.LastLine(), "expected ';' after typedef enum '" + name.text + "'");
                aliases[name.text] = ArgClass::I32;
                continue;
            }
            RetKind ignoredRet = RetKind::I32;
            const ArgClass aliasedClass = ParseType(cur, ignoredRet, /*allowVoid=*/false);
            if (cur.Eof() || IsBaseTypeWord(cur.Peek().text) || cur.Peek().text == "*" ||
                cur.Peek().text == ";")
                Fail(cur.Eof() ? cur.LastLine() : cur.Peek().line, "expected typedef name");
            const Token name = cur.Next();
            if (cur.Eof() || cur.Next().text != ";")
                Fail(cur.Eof() ? cur.LastLine() : cur.LastLine(),
                     "expected ';' after typedef '" + name.text + "'");
            aliases[name.text] = aliasedClass;
            continue;
        }

        if (cur.Peek().text == "enum")
        {
            SkipEnumDefinition(cur);
            if (!cur.Eof() && cur.Peek().text == ";") cur.Next();
            continue;
        }

        Prototype p;

        // Return type (may be void).
        RetKind ret = RetKind::I32;
        std::string retStructName;
        ArgClass retCls = ParseType(cur, ret, /*allowVoid=*/true, &retStructName);
        p.ret = retCls == ArgClass::Struct ? RetKind::Struct : ret;
        p.retStructName = std::move(retStructName);

        // Windows headers commonly place a calling-convention macro between
        // the return type and function name. On 64-bit ABIs those macros map
        // to the platform default convention used by libffi. Do not silently
        // accept them where that is not true.
        if (!cur.Eof() && IsCallingConvention(cur.Peek().text))
        {
            const Token convention = cur.Next();
            if (!kDefaultAbiMatchesWindowsConvention)
                Fail(convention.line, "calling convention '" + convention.text +
                                      "' is not supported on this architecture");
        }

        // Function name.
        if (cur.Eof() || cur.Peek().text == ";" || cur.Peek().text == "(" || cur.Peek().text == ")")
            Fail(cur.Eof() ? cur.LastLine() : cur.Peek().line, "expected function name after return type");
        p.name = cur.Next().text;

        if (cur.Next().text != "(")
            Fail(tokens.back().line, "expected '(' after function name");

        // Parameter list: empty, void, or comma-separated types. Parameter
        // names are optional and ignored when present. "(void)" is the
        // canonical empty parameter list.
        bool first = true;
        const bool isVoidParameterList =
            !cur.Eof() && cur.Peek().text == "void" &&
            cur.pos + 1 < cur.toks->size() &&
            (*cur.toks)[cur.pos + 1].text == ")";
        if (isVoidParameterList)
        {
            cur.Next(); // consume 'void'
            cur.Next(); // consume ')'
        }
        else if (!cur.Eof() && cur.Peek().text != ")")
        {
            while (true)
            {
                RetKind unused = RetKind::I32;
                std::string structName;
                ArgClass cls = ParseType(cur, unused, /*allowVoid=*/first, &structName);
                if (cls == ArgClass::I32 && unused == RetKind::Void)
                    Fail(cur.Peek().line, "'void' is not a valid parameter type");
                if (!cur.Eof() && cur.Peek().text == "(")
                {
                    const size_t callbackLine = cur.Next().line;
                    if (cur.Eof() || cur.Next().text != "*")
                        Fail(callbackLine, "expected '*' in function pointer parameter");
                    if (!cur.Eof() && cur.Peek().text != ")") cur.Next();
                    if (cur.Eof() || cur.Next().text != ")" || cur.Eof() || cur.Next().text != "(")
                        Fail(callbackLine, "malformed function pointer parameter");
                    size_t depth = 1;
                    while (!cur.Eof() && depth != 0)
                    {
                        const Token token = cur.Next();
                        if (token.text == "(") ++depth;
                        if (token.text == ")") --depth;
                    }
                    if (depth != 0)
                        Fail(callbackLine, "unterminated function pointer parameter");
                    cls = ArgClass::Ptr;
                }
                else if (!cur.Eof() && cur.Peek().text != "," && cur.Peek().text != ")" &&
                    cur.Peek().text != "*" && !IsBaseTypeWord(cur.Peek().text))
                {
                    // Optional parameter name: consume it.
                    cur.Next();
                }

                // C adjusts a fixed-size array parameter to a pointer. The
                // extent is useful documentation at the call site but does
                // not participate in the platform ABI, so v1 deliberately
                // represents T name[N] as the same pointer class as T*.
                if (!cur.Eof() && cur.Peek().text == "[")
                {
                    const size_t arrayLine = cur.Next().line;
                    if (cur.Eof() || cur.Peek().text == "]")
                        Fail(arrayLine, "array parameter requires a fixed size");
                    const Token extent = cur.Next();
                    if (extent.text.empty() || !std::isdigit(static_cast<unsigned char>(extent.text.front())))
                        Fail(extent.line, "array parameter size must be an integer literal");
                    if (cur.Eof() || cur.Next().text != "]")
                        Fail(extent.line, "expected ']' after array parameter size");
                    cls = ArgClass::Ptr;
                }
                p.args.push_back(cls);
                p.argStructNames.push_back(std::move(structName));
                first = false;
                if (cur.Eof()) Fail(cur.LastLine(), "expected ')' in parameter list");
                const Token sep = cur.Next();
                const size_t sepLine = sep.line;
                if (sep.text == ")") break;
                if (sep.text != ",") Fail(sepLine, "expected ',' or ')' in parameter list");
            }
        }
        else
        {
            cur.Next(); // consume ')'
        }

        if (p.args.size() > kMaxArity)
            Fail(cur.Eof() ? cur.LastLine() : cur.Peek().line,
                 "too many parameters (" + std::to_string(p.args.size()) +
                                      "); maximum supported arity is " + std::to_string(kMaxArity));

        // Optional trailing ';' (also tolerate grouped decls without one).
        if (!cur.Eof() && cur.Peek().text == ";") cur.Next();

        protos.push_back(std::move(p));
    }

    return {std::move(protos), std::move(structs)};
}

} // namespace lodeffi
