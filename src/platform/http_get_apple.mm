// http_get (platform/http_get.h) on macOS and iOS: NSURLSession.
#include "platform/http_get.h"

#import <Foundation/Foundation.h>

namespace chaotix {

bool http_get(const std::string& url, std::string* body) {
    @autoreleasepool {
        NSURL* u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (!u || ![u.scheme isEqualToString:@"https"]) return false;
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:u
                                                               cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                           timeoutInterval:8.0];
        [request setValue:@"ChaotixRecompiled" forHTTPHeaderField:@"User-Agent"];
        __block NSData* got = nil;
        __block NSInteger status = 0;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        NSURLSessionDataTask* task = [[NSURLSession sharedSession]
            dataTaskWithRequest:request
              completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                  if (!error && [response isKindOfClass:[NSHTTPURLResponse class]]) {
                      status = ((NSHTTPURLResponse*)response).statusCode;
                      got = data;
                  }
                  dispatch_semaphore_signal(done);
              }];
        [task resume];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);  // the session's timeout ends it
        if (status != 200 || !got || got.length > (1u << 20)) return false;
        body->assign(static_cast<const char*>(got.bytes), got.length);
        return true;
    }
}

}  // namespace chaotix
