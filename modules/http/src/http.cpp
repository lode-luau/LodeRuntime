// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Coroutine.hpp"
#include "Lode/Json.hpp"
#include "Lode/Module.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Task.hpp"
#include "Lode/Value.hpp"

#include "lua.h"
#include "http_client.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{

class BodyStream
{
public:
    std::shared_ptr<std::string> data = std::make_shared<std::string>();
    size_t offset = 0;

    size_t Available() const
    {
        return data && offset < data->size() ? data->size() - offset : 0;
    }

    std::string Take(size_t requested)
    {
        size_t available = Available();
        size_t take = requested > 0 ? (std::min)(requested, available) : available;
        std::string out;
        if (take > 0)
            out.assign(*data, offset, take);
        offset += take;
        return out;
    }

    Lode::Value read(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        size_t wanted = 0;
        if (args.size() > 1 && args[1].IsNumber())
        {
            auto result = Lode::Numeric::ToSize(args[1].AsNumber(), "read length");
            if (result.IsError())
            {
                vm.RaiseError(result.GetError().ErrorMessage());
                return Lode::Value();
            }
            wanted = result.GetValue();
        }
        return Lode::Value(Take(wanted));
    }

    Lode::Value readLine(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        (void)vm;
        (void)args;
        if (!data || offset >= data->size())
            return Lode::Value();
        size_t nl = data->find('\n', offset);
        if (nl == std::string::npos)
            return Lode::Value(Take(data->size() - offset));
        std::string line = data->substr(offset, nl - offset);
        offset = nl + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return Lode::Value(line);
    }

    Lode::Value readBuffer(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        size_t wanted = 0;
        if (args.size() > 1 && args[1].IsNumber())
        {
            auto result = Lode::Numeric::ToSize(args[1].AsNumber(), "read length");
            if (result.IsError())
            {
                vm.RaiseError(result.GetError().ErrorMessage());
                return Lode::Value();
            }
            wanted = result.GetValue();
        }
        std::string taken = Take(wanted);
        Lode::Value buf = vm.CreateBuffer(taken.size());
        size_t size = 0;
        void* ptr = buf.AsBuffer(&size);
        if (ptr && !taken.empty())
            std::memcpy(ptr, taken.data(), taken.size());
        return buf;
    }

    size_t CopyInto(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        size_t bufSize = 0;
        void* ptr = args[1].AsBuffer(&bufSize);
        size_t offset = 0;
        size_t length = 0;
        if (args.size() > 2 && args[2].IsNumber())
        {
            auto result = Lode::Numeric::ToSize(args[2].AsNumber(), "buffer offset");
            if (result.IsError())
            {
                vm.RaiseError(result.GetError().ErrorMessage());
                return 0;
            }
            offset = result.GetValue();
        }
        if (args.size() > 3 && args[3].IsNumber())
        {
            auto result = Lode::Numeric::ToSize(args[3].AsNumber(), "read length");
            if (result.IsError())
            {
                vm.RaiseError(result.GetError().ErrorMessage());
                return 0;
            }
            length = result.GetValue();
        }
        if (!ptr || offset >= bufSize)
            return 0;
        size_t capacity = (std::min)(length == 0 ? bufSize - offset : length, bufSize - offset);
        size_t available = Available();
        size_t copy = (std::min)(capacity, available);
        if (copy > 0)
        {
            std::memcpy(static_cast<uint8_t*>(ptr) + offset, data->data() + this->offset, copy);
            this->offset += copy;
        }
        return copy;
    }

    Lode::Value readInto(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        if (args.size() < 2 || !args[1].IsBuffer())
        {
            vm.RaiseError("http body stream readInto: buf must be a buffer");
            return Lode::Value();
        }
        return Lode::Value(static_cast<double>(CopyInto(vm, args)));
    }

    Lode::Value readAsync(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        if (args.size() < 2 || !args[1].IsFunction())
        {
            vm.RaiseError("http body stream readAsync: callback must be a function");
            return Lode::Value();
        }
        size_t wanted = 0;
        if (args.size() > 2 && args[2].IsNumber())
        {
            auto result = Lode::Numeric::ToSize(args[2].AsNumber(), "read length");
            if (result.IsError())
            {
                vm.RaiseError(result.GetError().ErrorMessage());
                return Lode::Value();
            }
            wanted = result.GetValue();
        }
        Lode::Value callback = args[1];
        std::string taken = Take(wanted);
        Lode::Task::Spawn(vm, callback, {Lode::Value(taken)});
        return Lode::Value();
    }

    Lode::Value readBufferAsync(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        if (args.size() < 2 || !args[1].IsFunction())
        {
            vm.RaiseError("http body stream readBufferAsync: callback must be a function");
            return Lode::Value();
        }
        size_t wanted = 0;
        if (args.size() > 2 && args[2].IsNumber())
        {
            auto result = Lode::Numeric::ToSize(args[2].AsNumber(), "read length");
            if (result.IsError())
            {
                vm.RaiseError(result.GetError().ErrorMessage());
                return Lode::Value();
            }
            wanted = result.GetValue();
        }
        Lode::Value callback = args[1];
        std::string taken = Take(wanted);
        Lode::Value buf = vm.CreateBuffer(taken.size());
        size_t size = 0;
        void* ptr = buf.AsBuffer(&size);
        if (ptr && !taken.empty())
            std::memcpy(ptr, taken.data(), taken.size());
        Lode::Task::Spawn(vm, callback, {buf});
        return Lode::Value();
    }

    Lode::Value readIntoAsync(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        if (args.size() < 3 || !args[1].IsBuffer() || !args[2].IsFunction())
        {
            vm.RaiseError("http body stream readIntoAsync: expected buf and callback");
            return Lode::Value();
        }
        Lode::Value callback = args[2];
        size_t copied = CopyInto(vm, args);
        Lode::Task::Spawn(vm, callback, {Lode::Value(static_cast<double>(copied))});
        return Lode::Value();
    }
};

struct BodyStreamClass
{
    Lode::Table metatable;
};

Lode::Table BuildBodyStreamMethods(Lode::State& vm)
{
    Lode::Table methods = vm.CreateTable();
    methods.Set("read", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<BodyStream>::Unwrap(vm, 1);
        if (!self)
        {
            vm.RaiseError("http body stream method called without a stream");
            return Lode::Value();
        }
        return self->read(vm, args);
    }));
    methods.Set("readBuffer", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<BodyStream>::Unwrap(vm, 1);
        if (!self)
        {
            vm.RaiseError("http body stream method called without a stream");
            return Lode::Value();
        }
        return self->readBuffer(vm, args);
    }));
    methods.Set("readLine", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<BodyStream>::Unwrap(vm, 1);
        if (!self)
        {
            vm.RaiseError("http body stream method called without a stream");
            return Lode::Value();
        }
        return self->readLine(vm, args);
    }));
    methods.Set("readInto", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<BodyStream>::Unwrap(vm, 1);
        if (!self)
        {
            vm.RaiseError("http body stream method called without a stream");
            return Lode::Value();
        }
        return self->readInto(vm, args);
    }));
    methods.Set("readAsync", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<BodyStream>::Unwrap(vm, 1);
        if (!self)
        {
            vm.RaiseError("http body stream method called without a stream");
            return Lode::Value();
        }
        return self->readAsync(vm, args);
    }));
    methods.Set("readBufferAsync", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<BodyStream>::Unwrap(vm, 1);
        if (!self)
        {
            vm.RaiseError("http body stream method called without a stream");
            return Lode::Value();
        }
        return self->readBufferAsync(vm, args);
    }));
    methods.Set("readIntoAsync", vm.CreateFunction([](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<BodyStream>::Unwrap(vm, 1);
        if (!self)
        {
            vm.RaiseError("http body stream method called without a stream");
            return Lode::Value();
        }
        return self->readIntoAsync(vm, args);
    }));
    return methods;
}

BodyStreamClass BuildBodyStreamClass(Lode::State& vm)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", Lode::Value(BuildBodyStreamMethods(vm)));
    meta.Set("__tostring", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value("http body stream");
    }));
    return {meta};
}

Lode::Value MakeBodyStream(Lode::State& vm, const BodyStreamClass& cls, std::shared_ptr<std::string> data)
{
    auto stream = std::make_shared<BodyStream>();
    stream->data = std::move(data);
    Lode::ObjectWrap<BodyStream>::Wrap(vm, stream, cls.metatable);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

Lode::Table BuildResponse(Lode::State& vm, const BodyStreamClass& cls,
                          const std::shared_ptr<lodehttp::HttpResponseData>& data)
{
    Lode::Table res = vm.CreateTable();
    res.Set("status", Lode::Value(data->status));
    res.Set("statusText", Lode::Value(data->statusText));
    res.Set("version", Lode::Value(data->version));
    res.Set("url", Lode::Value(data->finalUrl));
    Lode::Table headers = vm.CreateTable();
    for (const auto& h : data->headers)
        headers.Set(h.name, Lode::Value(h.value));
    res.Set("headers", Lode::Value(headers));
    auto body = std::make_shared<std::string>(std::move(data->body));
    res.Set("body", MakeBodyStream(vm, cls, body));
    res.Set("text", vm.CreateFunction([body](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(*body);
    }));
    res.Set("json", vm.CreateFunction([body](Lode::State& vm, const std::vector<Lode::Value>&) -> Lode::Value {
        Lode::Result<Lode::Value> parsed = Lode::Json::Parse(vm, *body);
        if (parsed.IsError())
        {
            vm.RaiseError("http response json: " + parsed.GetError().ErrorMessage());
            return Lode::Value();
        }
        return parsed.GetValue();
    }));
    return res;
}

std::string ErrorMessage(const std::shared_ptr<lodehttp::HttpResponseData>& result)
{
    return result->errorKind + ": " + result->errorMessage;
}

void NotifyYield(lua_State* mainL, Lode::Coroutine co,
                 const std::shared_ptr<lodehttp::HttpResponseData>& result, const BodyStreamClass& cls)
{
    Lode::State vm(mainL);
    Lode::Result<std::vector<Lode::Value>> resume = [&]() -> Lode::Result<std::vector<Lode::Value>> {
        if (result->errorKind.empty())
        {
            return co.Resume({Lode::Value(BuildResponse(vm, cls, result))});
        }
        return co.ResumeError(ErrorMessage(result));
    }();
    if (resume.IsError() && Lode::Task::IsMainThread(vm, co.GetThreadState()))
        Lode::Task::SetMainThreadError(vm, resume.GetError().ErrorMessage());
}

void NotifyCallback(lua_State* mainL, const Lode::Value& callback,
                    const std::shared_ptr<lodehttp::HttpResponseData>& result, const BodyStreamClass& cls)
{
    Lode::State vm(mainL);
    if (!callback.IsFunction())
        return;
    if (result->errorKind.empty())
    {
        Lode::Task::Spawn(vm, callback, {Lode::Value(BuildResponse(vm, cls, result)), Lode::Value()});
    }
    else
    {
        Lode::Task::Spawn(vm, callback, {Lode::Value(), Lode::Value(ErrorMessage(result))});
    }
}

std::string ToLowerAscii(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ParseUrlArg(const std::vector<Lode::Value>& args, std::string& url, std::string& error)
{
    if (args.size() < 1 || !args[0].IsString())
    {
        error = "url must be a string";
        return false;
    }
    url = args[0].AsString();
    return true;
}

bool ParseBodyArg(const std::vector<Lode::Value>& args, size_t index, std::string& body, std::string& error)
{
    if (args.size() <= index || args[index].IsNil())
        return true;
    if (args[index].IsString())
    {
        body = args[index].AsString();
        return true;
    }
    if (args[index].IsBuffer())
    {
        size_t size = 0;
        void* ptr = args[index].AsBuffer(&size);
        if (ptr && size > 0)
            body.assign(static_cast<const char*>(ptr), size);
        return true;
    }
    error = "body must be a string or buffer";
    return false;
}

bool ParseOptsTable(const Lode::Table& t, lodehttp::HttpRequestOptions& opts, std::string& error)
{
    auto method = t.Get("method");
    if (method.IsOk() && method.GetValue().IsString())
        opts.method = lodehttp::NormalizeMethod(method.GetValue().AsString());

    auto headers = t.Get("headers");
    if (headers.IsOk() && headers.GetValue().IsTable())
    {
        Lode::Table h = headers.GetValue().AsTable();
        for (const std::string& key : h.GetKeys())
        {
            auto value = h.Get(key);
            if (!value.IsOk() || !value.GetValue().IsString())
            {
                error = "header values must be strings";
                return false;
            }
            std::string name = ToLowerAscii(key);
            std::string headerValue = value.GetValue().AsString();
            if (name.find_first_of(":\r\n") != std::string::npos ||
                headerValue.find_first_of("\r\n") != std::string::npos)
            {
                error = "invalid header name or value";
                return false;
            }
            opts.headers.push_back({name, headerValue});
        }
    }

    auto body = t.Get("body");
    if (body.IsOk() && !body.GetValue().IsNil())
    {
        const Lode::Value& b = body.GetValue();
        if (b.IsString())
        {
            opts.body = b.AsString();
        }
        else if (b.IsBuffer())
        {
            size_t size = 0;
            void* ptr = b.AsBuffer(&size);
            if (ptr && size > 0)
                opts.body.assign(static_cast<const char*>(ptr), size);
        }
        else
        {
            error = "body must be a string or buffer";
            return false;
        }
    }

    auto timeout = t.Get("timeout");
    if (timeout.IsOk() && timeout.GetValue().IsNumber())
    {
        auto ms = Lode::Numeric::ToMilliseconds(timeout.GetValue().AsNumber(), 1.0, "timeout");
        if (ms.IsError())
        {
            error = ms.GetError().ErrorMessage();
            return false;
        }
        opts.timeoutMs = ms.GetValue();
    }

    auto redirect = t.Get("redirect");
    if (redirect.IsOk() && redirect.GetValue().IsString())
        opts.followRedirects = redirect.GetValue().AsString() == "follow";

    auto maxRedirects = t.Get("maxRedirects");
    if (maxRedirects.IsOk() && maxRedirects.GetValue().IsNumber())
    {
        double value = maxRedirects.GetValue().AsNumber();
        if (!std::isfinite(value) || value < 0 || std::trunc(value) != value)
        {
            error = "maxRedirects must be a non-negative integer";
            return false;
        }
        opts.maxRedirects = static_cast<int64_t>(value);
    }
    return true;
}

Lode::Value StartYield(Lode::State& vm, const std::string& url, lodehttp::HttpRequestOptions opts,
                       const std::shared_ptr<lodehttp::HttpManager>& mgr, const BodyStreamClass& cls)
{
    auto result = std::make_shared<lodehttp::HttpResponseData>();
    lua_State* mainL = vm.GetMainThread();
    Lode::Coroutine co(vm.GetLuaState());
    mgr->Start(url, opts, result, [mainL, co, result, cls]() {
        NotifyYield(mainL, co, result, cls);
    });
    return vm.YieldThread();
}

Lode::Value StartCallback(Lode::State& vm, const std::string& url, lodehttp::HttpRequestOptions opts,
                          const Lode::Value& callback,
                          const std::shared_ptr<lodehttp::HttpManager>& mgr, const BodyStreamClass& cls)
{
    auto result = std::make_shared<lodehttp::HttpResponseData>();
    lua_State* mainL = vm.GetMainThread();
    mgr->Start(url, opts, result, [mainL, callback, result, cls]() {
        NotifyCallback(mainL, callback, result, cls);
    });
    return Lode::Value();
}

Lode::Table BuildStatusCodes(Lode::State& vm)
{
    Lode::Table t = vm.CreateTable();
    t.Set("CONTINUE", Lode::Value(static_cast<double>(100)));
    t.Set("SWITCHING_PROTOCOLS", Lode::Value(static_cast<double>(101)));
    t.Set("PROCESSING", Lode::Value(static_cast<double>(102)));
    t.Set("EARLY_HINTS", Lode::Value(static_cast<double>(103)));
    t.Set("OK", Lode::Value(static_cast<double>(200)));
    t.Set("CREATED", Lode::Value(static_cast<double>(201)));
    t.Set("ACCEPTED", Lode::Value(static_cast<double>(202)));
    t.Set("NON_AUTHORITATIVE_INFORMATION", Lode::Value(static_cast<double>(203)));
    t.Set("NO_CONTENT", Lode::Value(static_cast<double>(204)));
    t.Set("RESET_CONTENT", Lode::Value(static_cast<double>(205)));
    t.Set("PARTIAL_CONTENT", Lode::Value(static_cast<double>(206)));
    t.Set("MULTI_STATUS", Lode::Value(static_cast<double>(207)));
    t.Set("ALREADY_REPORTED", Lode::Value(static_cast<double>(208)));
    t.Set("IM_USED", Lode::Value(static_cast<double>(226)));
    t.Set("MULTIPLE_CHOICES", Lode::Value(static_cast<double>(300)));
    t.Set("MOVED_PERMANENTLY", Lode::Value(static_cast<double>(301)));
    t.Set("FOUND", Lode::Value(static_cast<double>(302)));
    t.Set("SEE_OTHER", Lode::Value(static_cast<double>(303)));
    t.Set("NOT_MODIFIED", Lode::Value(static_cast<double>(304)));
    t.Set("USE_PROXY", Lode::Value(static_cast<double>(305)));
    t.Set("TEMPORARY_REDIRECT", Lode::Value(static_cast<double>(307)));
    t.Set("PERMANENT_REDIRECT", Lode::Value(static_cast<double>(308)));
    t.Set("BAD_REQUEST", Lode::Value(static_cast<double>(400)));
    t.Set("UNAUTHORIZED", Lode::Value(static_cast<double>(401)));
    t.Set("PAYMENT_REQUIRED", Lode::Value(static_cast<double>(402)));
    t.Set("FORBIDDEN", Lode::Value(static_cast<double>(403)));
    t.Set("NOT_FOUND", Lode::Value(static_cast<double>(404)));
    t.Set("METHOD_NOT_ALLOWED", Lode::Value(static_cast<double>(405)));
    t.Set("NOT_ACCEPTABLE", Lode::Value(static_cast<double>(406)));
    t.Set("PROXY_AUTHENTICATION_REQUIRED", Lode::Value(static_cast<double>(407)));
    t.Set("REQUEST_TIMEOUT", Lode::Value(static_cast<double>(408)));
    t.Set("CONFLICT", Lode::Value(static_cast<double>(409)));
    t.Set("GONE", Lode::Value(static_cast<double>(410)));
    t.Set("LENGTH_REQUIRED", Lode::Value(static_cast<double>(411)));
    t.Set("PRECONDITION_FAILED", Lode::Value(static_cast<double>(412)));
    t.Set("PAYLOAD_TOO_LARGE", Lode::Value(static_cast<double>(413)));
    t.Set("URI_TOO_LONG", Lode::Value(static_cast<double>(414)));
    t.Set("UNSUPPORTED_MEDIA_TYPE", Lode::Value(static_cast<double>(415)));
    t.Set("RANGE_NOT_SATISFIABLE", Lode::Value(static_cast<double>(416)));
    t.Set("EXPECTATION_FAILED", Lode::Value(static_cast<double>(417)));
    t.Set("IM_A_TEAPOT", Lode::Value(static_cast<double>(418)));
    t.Set("MISDIRECTED_REQUEST", Lode::Value(static_cast<double>(421)));
    t.Set("UNPROCESSABLE_CONTENT", Lode::Value(static_cast<double>(422)));
    t.Set("LOCKED", Lode::Value(static_cast<double>(423)));
    t.Set("FAILED_DEPENDENCY", Lode::Value(static_cast<double>(424)));
    t.Set("TOO_EARLY", Lode::Value(static_cast<double>(425)));
    t.Set("UPGRADE_REQUIRED", Lode::Value(static_cast<double>(426)));
    t.Set("PRECONDITION_REQUIRED", Lode::Value(static_cast<double>(428)));
    t.Set("TOO_MANY_REQUESTS", Lode::Value(static_cast<double>(429)));
    t.Set("REQUEST_HEADER_FIELDS_TOO_LARGE", Lode::Value(static_cast<double>(431)));
    t.Set("UNAVAILABLE_FOR_LEGAL_REASONS", Lode::Value(static_cast<double>(451)));
    t.Set("INTERNAL_SERVER_ERROR", Lode::Value(static_cast<double>(500)));
    t.Set("NOT_IMPLEMENTED", Lode::Value(static_cast<double>(501)));
    t.Set("BAD_GATEWAY", Lode::Value(static_cast<double>(502)));
    t.Set("SERVICE_UNAVAILABLE", Lode::Value(static_cast<double>(503)));
    t.Set("GATEWAY_TIMEOUT", Lode::Value(static_cast<double>(504)));
    t.Set("HTTP_VERSION_NOT_SUPPORTED", Lode::Value(static_cast<double>(505)));
    t.Set("VARIANT_ALSO_NEGOTIATES", Lode::Value(static_cast<double>(506)));
    t.Set("INSUFFICIENT_STORAGE", Lode::Value(static_cast<double>(507)));
    t.Set("LOOP_DETECTED", Lode::Value(static_cast<double>(508)));
    t.Set("NOT_EXTENDED", Lode::Value(static_cast<double>(510)));
    t.Set("NETWORK_AUTHENTICATION_REQUIRED", Lode::Value(static_cast<double>(511)));
    return t;
}

} // namespace

LODE_MODULE(vm)
{
    auto mgr = std::make_shared<lodehttp::HttpManager>(vm.GetMainThread());
    Lode::Task::RegisterShutdownHook(vm, [mgr]() {
        mgr->Shutdown();
    });

    BodyStreamClass cls = BuildBodyStreamClass(vm);

    Lode::Exports exports(vm);
    exports.Function("request", [mgr, cls](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string url;
        lodehttp::HttpRequestOptions opts;
        std::string error;
        if (!ParseUrlArg(args, url, error))
        {
            vm.RaiseError("http.request: " + error);
            return Lode::Value();
        }
        if (args.size() > 1 && args[1].IsTable() && !ParseOptsTable(args[1].AsTable(), opts, error))
        {
            vm.RaiseError("http.request: " + error);
            return Lode::Value();
        }
        return StartYield(vm, url, opts, mgr, cls);
    });

    exports.Function("requestWithCallback", [mgr, cls](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.size() < 3 || !args[2].IsFunction())
        {
            vm.RaiseError("http.requestWithCallback: callback must be a function");
            return Lode::Value();
        }
        std::string url;
        lodehttp::HttpRequestOptions opts;
        std::string error;
        if (!ParseUrlArg(args, url, error))
        {
            vm.RaiseError("http.requestWithCallback: " + error);
            return Lode::Value();
        }
        if (!ParseOptsTable(args[1].AsTable(), opts, error))
        {
            vm.RaiseError("http.requestWithCallback: " + error);
            return Lode::Value();
        }
        return StartCallback(vm, url, opts, args[2], mgr, cls);
    });

    exports.Function("get", [mgr, cls](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string url;
        lodehttp::HttpRequestOptions opts;
        std::string error;
        if (!ParseUrlArg(args, url, error))
        {
            vm.RaiseError("http.get: " + error);
            return Lode::Value();
        }
        if (args.size() > 1 && args[1].IsTable() && !ParseOptsTable(args[1].AsTable(), opts, error))
        {
            vm.RaiseError("http.get: " + error);
            return Lode::Value();
        }
        opts.method = "GET";
        return StartYield(vm, url, opts, mgr, cls);
    });

    exports.Function("getWithCallback", [mgr, cls](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.size() < 3 || !args[2].IsFunction())
        {
            vm.RaiseError("http.getWithCallback: callback must be a function");
            return Lode::Value();
        }
        std::string url;
        lodehttp::HttpRequestOptions opts;
        std::string error;
        if (!ParseUrlArg(args, url, error))
        {
            vm.RaiseError("http.getWithCallback: " + error);
            return Lode::Value();
        }
        if (!ParseOptsTable(args[1].AsTable(), opts, error))
        {
            vm.RaiseError("http.getWithCallback: " + error);
            return Lode::Value();
        }
        opts.method = "GET";
        return StartCallback(vm, url, opts, args[2], mgr, cls);
    });

    exports.Function("post", [mgr, cls](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string url;
        lodehttp::HttpRequestOptions opts;
        std::string error;
        if (!ParseUrlArg(args, url, error))
        {
            vm.RaiseError("http.post: " + error);
            return Lode::Value();
        }
        if (!ParseBodyArg(args, 1, opts.body, error))
        {
            vm.RaiseError("http.post: " + error);
            return Lode::Value();
        }
        if (args.size() > 2 && args[2].IsTable() && !ParseOptsTable(args[2].AsTable(), opts, error))
        {
            vm.RaiseError("http.post: " + error);
            return Lode::Value();
        }
        opts.method = "POST";
        return StartYield(vm, url, opts, mgr, cls);
    });

    exports.Function("postWithCallback", [mgr, cls](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.size() < 4 || !args[3].IsFunction())
        {
            vm.RaiseError("http.postWithCallback: callback must be a function");
            return Lode::Value();
        }
        std::string url;
        lodehttp::HttpRequestOptions opts;
        std::string error;
        if (!ParseUrlArg(args, url, error))
        {
            vm.RaiseError("http.postWithCallback: " + error);
            return Lode::Value();
        }
        if (!ParseBodyArg(args, 1, opts.body, error))
        {
            vm.RaiseError("http.postWithCallback: " + error);
            return Lode::Value();
        }
        if (!ParseOptsTable(args[2].AsTable(), opts, error))
        {
            vm.RaiseError("http.postWithCallback: " + error);
            return Lode::Value();
        }
        opts.method = "POST";
        return StartCallback(vm, url, opts, args[3], mgr, cls);
    });

    exports.Function("abort", [mgr](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        mgr->AbortAll();
        return Lode::Value();
    });

    exports.SetTable("statusCode", BuildStatusCodes(vm));
    return Lode::ModuleReturn(exports.GetExportTable());
}
