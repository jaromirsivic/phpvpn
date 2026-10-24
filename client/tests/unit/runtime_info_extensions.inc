PHPVPN_TEST(runtime, communication_contracts_order_and_recovery_once_per_epoch) {
    EngineFixture f;
    std::atomic<bool> failData{false};
    f.transport.hook=[&](HttpAction action,std::string_view,int)->std::optional<HttpExchangeResult>{
        if(action==HttpAction::data && failData.exchange(false))return HttpExchangeResult{Error(ErrorCode::transport_timeout),0};
        return {};
    };
    // Full recovery semantics are under test: soft recovery is disabled.
    VpnEngineTestAccess::softRecoveryGrace(f.engine,0);
    PHPVPN_REQUIRE_OK(f.engine.start());PHPVPN_REQUIRE_OK(f.engine.connect());
    PHPVPN_CHECK(eventually([&]{return f.engine.status().state==ConnectionState::connected;}));
    const auto requests=f.transport.observedHeaders();PHPVPN_CHECK(requests.size()>=4);
    if(requests.size()<4)return;
    PHPVPN_CHECK_EQ(requests[0].action,HttpAction::health);PHPVPN_CHECK_EQ(requests[1].action,HttpAction::info);
    PHPVPN_CHECK_EQ(requests[2].action,HttpAction::data);PHPVPN_CHECK_EQ(requests[3].action,HttpAction::info);
    auto standard=Json::parse(requests[1].request).value();auto extended=Json::parse(requests[3].request).value();
    PHPVPN_CHECK(standard.find("maskedCommunicationContracts")==nullptr);
    PHPVPN_CHECK(extended.find("maskedCommunicationContracts")->isArray());
    PHPVPN_CHECK(extended.find("includeMaskedCommunicationContracts")->boolValue());
    PHPVPN_CHECK_EQ(f.engine.status().communicationContractsCheck,CheckpointResult::success);
    PHPVPN_CHECK_EQ(f.transport.extendedCalls.load(),1);
    failData=true;
    PHPVPN_CHECK(eventually([&]{return f.engine.status().state==ConnectionState::recovering;}));
    f.clock.now+=10000;
    PHPVPN_CHECK(eventually([&]{return f.engine.status().state==ConnectionState::connected;}));
    PHPVPN_CHECK_EQ(f.transport.extendedCalls.load(),1);
    PHPVPN_REQUIRE_OK(f.engine.disconnect());PHPVPN_CHECK(eventually([&]{return f.engine.status().state==ConnectionState::disconnected;}));
    PHPVPN_REQUIRE_OK(f.engine.connect());PHPVPN_CHECK(eventually([&]{return f.engine.status().state==ConnectionState::connected;}));
    PHPVPN_CHECK_EQ(f.transport.extendedCalls.load(),2);
}
PHPVPN_TEST(runtime, communication_contracts_failure_is_terminal_without_capture) {
    for(int mode=0;mode<3;++mode) {
        EngineFixture f;std::atomic<int> extended{0};f.transport.suppressDiagnostic=true;
        f.transport.hook=[&](HttpAction action,std::string_view request,int)->std::optional<HttpExchangeResult>{
            if(action==HttpAction::info && request.find("includeMaskedCommunicationContracts")!=std::string_view::npos) {
                ++extended;
                if(mode==0)return HttpExchangeResult{Error(ErrorCode::transport_timeout),0};
                if(mode==1)return HttpExchangeResult{HttpResponse{401,"{}","application/json"},2};
                f.transport.badInfo=true;
            }
            return {};
        };
        PHPVPN_REQUIRE_OK(f.engine.start());PHPVPN_REQUIRE_OK(f.engine.connect());
        PHPVPN_CHECK(eventually([&]{return f.engine.status().state==ConnectionState::error;}));
        const auto status=f.engine.status();
        PHPVPN_CHECK_EQ(status.communicationContractsCheck,CheckpointResult::failed);
        PHPVPN_CHECK(allChecks(status,CheckpointResult::success,CheckpointResult::success,CheckpointResult::success));
        PHPVPN_CHECK(!status.lastError->recoverable);PHPVPN_CHECK_EQ(f.driver.starts.load(),0);
        PHPVPN_CHECK_EQ(status.lastError->code,mode==0?"communication_timeout":"communication_contracts_verification_failed");
        f.clock.now+=100000;std::this_thread::sleep_for(std::chrono::milliseconds(300));
        PHPVPN_CHECK_EQ(extended.load(),1);
    }
}
PHPVPN_TEST(runtime, communication_contracts_not_skipped_after_early_recovery) {
    EngineFixture f;
    f.transport.hook=[](HttpAction action,std::string_view,int call)->std::optional<HttpExchangeResult>{
        if(action==HttpAction::data && call==1)return HttpExchangeResult{Error(ErrorCode::transport_timeout),0};return {};
    };
    PHPVPN_REQUIRE_OK(f.engine.start());PHPVPN_REQUIRE_OK(f.engine.connect());
    PHPVPN_CHECK(eventually([&]{return f.engine.status().state==ConnectionState::recovering;}));
    PHPVPN_CHECK_EQ(f.transport.extendedCalls.load(),0);
    f.clock.now+=10000;PHPVPN_CHECK(eventually([&]{return f.engine.status().state==ConnectionState::connected;}));
    PHPVPN_CHECK_EQ(f.transport.extendedCalls.load(),1);PHPVPN_CHECK_EQ(f.engine.status().communicationContractsCheck,CheckpointResult::success);
}
