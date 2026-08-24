// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Http/HttpManager.hpp"
#include "Http/HttpClient.hpp"
#include "Http/HttpServer.hpp"
#include "Http/HttpHelpers.hpp"
#include "Lode/Module.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Numeric.hpp"

LODE_MODULE(vm)
{
    auto mgr = std::make_shared<lodehttp::HttpManager>();
    mgr->mainL = vm.GetMainThread();
    mgr->loop = vm.GetEventLoop().GetUVLoop();
    Lode::Task::RegisterShutdownHook(vm, [mgr]() { mgr->Shutdown(); });

    mgr->clientMethods = lodehttp::BuildClientMethods(vm, mgr);
    mgr->serverMethods = lodehttp::BuildServerMethods(vm, mgr);

    Lode::Table httpClientClass = vm.CreateTable();
    httpClientClass.Set("Create", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("http: runtime is shutting down");
            return Lode::Value();
        }
        auto client = std::make_shared<lodehttp::HttpClient>();
        client->mgr = mgr;
        client->mainL = mgr->mainL;
        client->loop = mgr->loop;
        Lode::Value clientOptionsValue;
        if (args.Size() > 1)
            clientOptionsValue = args[1].ToValue();
        else if (!args.empty())
            clientOptionsValue = args[0].ToValue();
        const Lode::Value* clientOptions = &clientOptionsValue;
        if (clientOptions && !clientOptions->IsNil())
        {
            if (!clientOptions->IsTable())
            {
                vm2.RaiseError("HttpClient.Create options must be a table");
                return Lode::Value();
            }
            auto options = clientOptions->AsTable();
            auto readSize = [&](const char* key, size_t& output, size_t maximum) -> bool {
                if (!options.Has(key)) return true;
                auto value = options.Get(key);
                if (!value.IsOk() || !value.GetValue().IsNumber())
                {
                    vm2.RaiseError(std::string("HttpClient.Create option '") + key + "' must be a number");
                    return false;
                }
                auto converted = Lode::Numeric::ToSize(value.GetValue().AsNumber(), key);
                if (converted.IsError() || converted.GetValue() > maximum)
                {
                    vm2.RaiseError(std::string("HttpClient.Create option '") + key + "' is out of range");
                    return false;
                }
                output = converted.GetValue();
                return true;
            };
            size_t idleTimeout = static_cast<size_t>(client->idleTimeoutMs);
            if (!readSize("maxIdleConnections", client->maxIdleConnections, 64) ||
                !readSize("maxConnectionUses", client->maxConnectionUses, 1000000) ||
                !readSize("idleTimeout", idleTimeout, 3600000))
                return Lode::Value();
            client->idleTimeoutMs = static_cast<uint64_t>(idleTimeout);
            if (client->maxConnectionUses == 0)
                client->maxConnectionUses = 1;
        }
        client->InitSignals(vm2);
        mgr->AddClient(client);
        client->selfGuard = client;
        return lodehttp::WrapClient(vm2, client, mgr->clientMethods);
    }));

    Lode::Table serverClass = vm.CreateTable();
    serverClass.Set("Create", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("http: runtime is shutting down");
            return Lode::Value();
        }
        auto server = std::make_shared<lodehttp::HttpServer>();
        server->mgr = mgr;
        server->mainL = mgr->mainL;
        server->loop = mgr->loop;
        server->responseMethods = lodehttp::BuildResponseMethods(vm2);
        server->InitSignals(vm2);

        for (const auto a : args)
        {
            if (a.IsTable())
            {
                auto t = a.AsTable();
                if (t.Has("tls") && t.Get("tls").GetValue().IsBoolean() && t.Get("tls").GetValue().AsBoolean())
                    server->isTls = true;
                if (t.Has("ssl") && t.Get("ssl").GetValue().IsBoolean() && t.Get("ssl").GetValue().AsBoolean())
                    server->isTls = true;
            }
        }

        mgr->AddServer(server);
        server->selfGuard = server;
        return lodehttp::WrapServer(vm2, server, mgr->serverMethods);
    }));

    Lode::Exports exports(vm);

    Lode::Table statusCodes = vm.CreateTable();
    statusCodes.Set("Continue", Lode::Value(100.0));
    statusCodes.Set("SwitchingProtocols", Lode::Value(101.0));
    statusCodes.Set("Processing", Lode::Value(102.0));
    statusCodes.Set("Ok", Lode::Value(200.0));
    statusCodes.Set("Created", Lode::Value(201.0));
    statusCodes.Set("Accepted", Lode::Value(202.0));
    statusCodes.Set("NonAuthoritativeInformation", Lode::Value(203.0));
    statusCodes.Set("NoContent", Lode::Value(204.0));
    statusCodes.Set("ResetContent", Lode::Value(205.0));
    statusCodes.Set("PartialContent", Lode::Value(206.0));
    statusCodes.Set("MultiStatus", Lode::Value(207.0));
    statusCodes.Set("AlreadyReported", Lode::Value(208.0));
    statusCodes.Set("IMUsed", Lode::Value(226.0));
    statusCodes.Set("MultipleChoices", Lode::Value(300.0));
    statusCodes.Set("MovedPermanently", Lode::Value(301.0));
    statusCodes.Set("Found", Lode::Value(302.0));
    statusCodes.Set("SeeOther", Lode::Value(303.0));
    statusCodes.Set("NotModified", Lode::Value(304.0));
    statusCodes.Set("UseProxy", Lode::Value(305.0));
    statusCodes.Set("TemporaryRedirect", Lode::Value(307.0));
    statusCodes.Set("PermanentRedirect", Lode::Value(308.0));
    statusCodes.Set("BadRequest", Lode::Value(400.0));
    statusCodes.Set("Unauthorized", Lode::Value(401.0));
    statusCodes.Set("PaymentRequired", Lode::Value(402.0));
    statusCodes.Set("Forbidden", Lode::Value(403.0));
    statusCodes.Set("NotFound", Lode::Value(404.0));
    statusCodes.Set("MethodNotAllowed", Lode::Value(405.0));
    statusCodes.Set("NotAcceptable", Lode::Value(406.0));
    statusCodes.Set("ProxyAuthenticationRequired", Lode::Value(407.0));
    statusCodes.Set("RequestTimeout", Lode::Value(408.0));
    statusCodes.Set("Conflict", Lode::Value(409.0));
    statusCodes.Set("Gone", Lode::Value(410.0));
    statusCodes.Set("LengthRequired", Lode::Value(411.0));
    statusCodes.Set("PreconditionFailed", Lode::Value(412.0));
    statusCodes.Set("PayloadTooLarge", Lode::Value(413.0));
    statusCodes.Set("UriTooLong", Lode::Value(414.0));
    statusCodes.Set("UnsupportedMediaType", Lode::Value(415.0));
    statusCodes.Set("RangeNotSatisfiable", Lode::Value(416.0));
    statusCodes.Set("ExpectationFailed", Lode::Value(417.0));
    statusCodes.Set("ImATeapot", Lode::Value(418.0));
    statusCodes.Set("MisdirectedRequest", Lode::Value(421.0));
    statusCodes.Set("UnprocessableEntity", Lode::Value(422.0));
    statusCodes.Set("Locked", Lode::Value(423.0));
    statusCodes.Set("FailedDependency", Lode::Value(424.0));
    statusCodes.Set("UpgradeRequired", Lode::Value(426.0));
    statusCodes.Set("PreconditionRequired", Lode::Value(428.0));
    statusCodes.Set("TooManyRequests", Lode::Value(429.0));
    statusCodes.Set("RequestHeaderFieldsTooLarge", Lode::Value(431.0));
    statusCodes.Set("UnavailableForLegalReasons", Lode::Value(451.0));
    statusCodes.Set("InternalServerError", Lode::Value(500.0));
    statusCodes.Set("NotImplemented", Lode::Value(501.0));
    statusCodes.Set("BadGateway", Lode::Value(502.0));
    statusCodes.Set("ServiceUnavailable", Lode::Value(503.0));
    statusCodes.Set("GatewayTimeout", Lode::Value(504.0));
    statusCodes.Set("HttpVersionNotSupported", Lode::Value(505.0));
    statusCodes.Set("VariantAlsoNegotiates", Lode::Value(506.0));
    statusCodes.Set("InsufficientStorage", Lode::Value(507.0));
    statusCodes.Set("LoopDetected", Lode::Value(508.0));
    statusCodes.Set("NotExtended", Lode::Value(510.0));
    statusCodes.Set("NetworkAuthenticationRequired", Lode::Value(511.0));

    exports.SetTable("Server", serverClass);
    exports.SetTable("HttpClient", httpClientClass);
    exports.SetTable("StatusCodes", statusCodes);

    exports.Function("fetch", [mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("http: runtime is shutting down");
            return Lode::Value();
        }
        if (args.empty() || !args[0].IsString())
        {
            vm2.RaiseError("fetch: expected url string");
            return Lode::Value();
        }
        std::string url = args[0].AsString();
        
        // Internally create an ad-hoc client to serve the single fetch request
        auto client = std::make_shared<lodehttp::HttpClient>();
        client->mgr = mgr;
        client->mainL = mgr->mainL;
        client->loop = mgr->loop;
        client->InitSignals(vm2);
        mgr->AddClient(client);
        client->selfGuard = client;
        
        auto req = std::make_shared<lodehttp::HttpRequestContext>(client);
        req->isAsync = false;
        req->selfGuard = req;
        req->taskCtx = Lode::Coroutine(vm2.GetLuaState());
        client->activeRequests.push_back(req);

        if (args.Size() > 1)
        {
            if (!lodehttp::ParseFetchOptions(vm2, args[1].ToValue(), req->opts))
            {
                req->taskCtx = Lode::Coroutine();
                client->activeRequests.pop_back();
                return Lode::Value();
            }
        }
        // One-shot fetch clients cannot share a connection with a later call.
        req->opts.keepAlive = false;
        std::string err = req->Begin(url);
        if (!err.empty())
        {
            req->taskCtx = Lode::Coroutine();
            client->activeRequests.pop_back();
            vm2.RaiseError(err);
            return Lode::Value();
        }
        return vm2.YieldThread();
    });

    return Lode::ModuleReturn(exports.GetExportTable());
}
