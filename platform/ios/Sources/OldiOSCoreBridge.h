#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
@interface OldiOSCoreBridge : NSObject
+ (NSString *)runtimeStatus;
+ (NSString *)inspectMachOAtPath:(NSString *)path;
+ (NSString *)inspectIPAAtPath:(NSString *)path;
+ (NSString *)traceIPAAtPath:(NSString *)path instructionLimit:(NSUInteger)instructionLimit;
@end
NS_ASSUME_NONNULL_END
