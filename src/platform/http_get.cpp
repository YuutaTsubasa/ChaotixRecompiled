#include "platform/http_get.h"

#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_WINDOWS)
#include <windows.h>
#include <winhttp.h>

#include <iterator>
#elif defined(SDL_PLATFORM_ANDROID)
#include <jni.h>
#elif defined(SDL_PLATFORM_LINUX)
#include <dlfcn.h>
#endif

namespace chaotix {

#if defined(SDL_PLATFORM_WINDOWS) || defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_LINUX)
namespace {
constexpr size_t kMaxBody = 1 << 20;
[[maybe_unused]] constexpr int kTimeoutMs = 8000;  // Android's is in ChaotixActivity.httpGet
}  // namespace
#endif

#if defined(SDL_PLATFORM_WINDOWS)

bool http_get(const std::string& url, std::string* body) {
    const std::wstring wurl(url.begin(), url.end());  // our own URLs: ASCII
    wchar_t host[256] = {}, path[2048] = {};
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    parts.lpszHostName = host;
    parts.dwHostNameLength = DWORD(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = DWORD(std::size(path));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return false;
    bool ok = false;
    HINTERNET session = WinHttpOpen(L"ChaotixRecompiled", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = nullptr, request = nullptr;
    if (session) {
        WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
        connection = WinHttpConnect(session, host, parts.nPort, 0);
    }
    if (connection)
        request = WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof status;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            body->clear();
            ok = true;
            for (;;) {
                char buf[8192];
                DWORD got = 0;
                if (!WinHttpReadData(request, buf, sizeof buf, &got)) { ok = false; break; }
                if (got == 0) break;
                body->append(buf, got);
                if (body->size() > kMaxBody) { ok = false; break; }
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

#elif defined(SDL_PLATFORM_ANDROID)

bool http_get(const std::string& url, std::string* body) {
    // SDL attaches this thread to the VM on first use and detaches it when
    // the thread ends. The activity's class, not FindClass: a thread of our
    // own only sees the system's classes through FindClass.
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = env ? static_cast<jobject>(SDL_GetAndroidActivity()) : nullptr;
    if (!activity) return false;
    jclass c = env->GetObjectClass(activity);
    env->DeleteLocalRef(activity);
    bool ok = false;
    if (jmethodID m = env->GetStaticMethodID(c, "httpGet", "(Ljava/lang/String;)Ljava/lang/String;")) {
        jstring jurl = env->NewStringUTF(url.c_str());
        jstring s = static_cast<jstring>(env->CallStaticObjectMethod(c, m, jurl));
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (s) {
            const char* utf = env->GetStringUTFChars(s, nullptr);
            *body = utf;
            env->ReleaseStringUTFChars(s, utf);
            env->DeleteLocalRef(s);
            ok = body->size() <= kMaxBody;
        }
        env->DeleteLocalRef(jurl);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(c);
    return ok;
}

#elif defined(SDL_PLATFORM_LINUX)

namespace {
// The few libcurl calls and options used, by their documented values, so
// building needs no curl headers and running needs no curl at all.
using CurlInit = void* (*)();
using CurlSetopt = int (*)(void*, int, ...);
using CurlPerform = int (*)(void*);
using CurlCleanup = void (*)(void*);
constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptUserAgent = 10018, kOptWriteFunction = 20011,
              kOptFailOnError = 45, kOptFollowLocation = 52, kOptNoSignal = 99, kOptTimeoutMs = 155,
              kOptProtocols = 181;
constexpr long kProtoHttps = 1 << 1;

size_t append(char* data, size_t size, size_t count, void* out) {
    std::string* body = static_cast<std::string*>(out);
    if (body->size() + size * count > kMaxBody) return 0;  // stops the transfer
    body->append(data, size * count);
    return size * count;
}
}  // namespace

bool http_get(const std::string& url, std::string* body) {
    void* lib = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    auto init = reinterpret_cast<CurlInit>(dlsym(lib, "curl_easy_init"));
    auto setopt = reinterpret_cast<CurlSetopt>(dlsym(lib, "curl_easy_setopt"));
    auto perform = reinterpret_cast<CurlPerform>(dlsym(lib, "curl_easy_perform"));
    auto cleanup = reinterpret_cast<CurlCleanup>(dlsym(lib, "curl_easy_cleanup"));
    bool ok = false;
    if (void* curl = init && setopt && perform && cleanup ? init() : nullptr) {
        body->clear();
        size_t (*write)(char*, size_t, size_t, void*) = append;
        setopt(curl, kOptUrl, url.c_str());
        setopt(curl, kOptProtocols, kProtoHttps);
        setopt(curl, kOptUserAgent, "ChaotixRecompiled");
        setopt(curl, kOptFollowLocation, 1L);
        setopt(curl, kOptFailOnError, 1L);
        setopt(curl, kOptNoSignal, 1L);
        setopt(curl, kOptTimeoutMs, long(kTimeoutMs));
        setopt(curl, kOptWriteFunction, write);
        setopt(curl, kOptWriteData, static_cast<void*>(body));
        ok = perform(curl) == 0;
        cleanup(curl);
    }
    // Left loaded: libcurl and the TLS library under it are not made to be
    // unloaded and loaded again.
    return ok;
}

#elif !defined(SDL_PLATFORM_APPLE)

bool http_get(const std::string&, std::string*) { return false; }

#endif  // macOS and iOS: platform/http_get_apple.mm

}  // namespace chaotix
