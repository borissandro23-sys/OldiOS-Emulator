#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
@interface OldiOSCoreBridge : NSObject
+ (NSString *)runtimeStatus;
+ (NSString *)inspectMachOAtPath:(NSString *)path;
+ (NSString *)inspectIPAAtPath:(NSString *)path;
@end
NS_ASSUME_NONNULL_END
