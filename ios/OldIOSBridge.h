#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
@interface OldIOSBridge : NSObject
+ (NSDictionary<NSString *, id> *)analyzeIPAAtPath:(NSString *)path;
@end
NS_ASSUME_NONNULL_END
