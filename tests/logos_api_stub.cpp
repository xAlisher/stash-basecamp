// Minimal LogosAPI + LogosAPIClient stubs — same pattern as logos-notes.
// test_stash_plugin never calls initLogos() so no real storage node starts.

#include "logos_api.h"
#include "cpp/logos_api_client.h"

LogosAPI::LogosAPI(const QString& /*module_name*/, QObject* parent)
    : QObject(parent), m_provider(nullptr), m_token_manager(nullptr) {}

LogosAPI::~LogosAPI() {}

LogosAPIProvider* LogosAPI::getProvider() const { return nullptr; }
LogosAPIClient*   LogosAPI::getClient(const QString&) const { return nullptr; }
TokenManager*     LogosAPI::getTokenManager() const { return nullptr; }

bool LogosAPIClient::isConnected() const { return false; }

QVariant LogosAPIClient::invokeRemoteMethod(
    const QString&, const QString&, const QVariant&, const QVariant&, Timeout)
{ return {}; }

QVariant LogosAPIClient::invokeRemoteMethod(
    const QString&, const QString&, const QVariant&, const QVariant&,
    const QVariant&, const QVariant&, Timeout)
{ return {}; }

void LogosAPIClient::onEvent(
    QObject*, QObject*, const QString&,
    std::function<void(const QString&, const QVariantList&)>)
{}

// Nim runtime globals — see nim_runtime_stub.cpp.
