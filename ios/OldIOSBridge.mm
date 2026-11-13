#import "OldIOSBridge.h"

#include "armv6.hpp"
#include "ipa_loader.hpp"
#include "macho_loader.hpp"

#include <filesystem>
#include <string>

static NSString *ToNSString(const std::string& value) {
    return [[NSString alloc] initWithBytes:value.data()
                                    length:value.size()
                                  encoding:NSUTF8StringEncoding] ?: @"(unreadable text)";
}

@implementation OldIOSBridge
+ (NSDictionary<NSString *, id> *)analyzeIPAAtPath:(NSString *)path {
    @autoreleasepool {
        IpaInfo ipa;
        const std::filesystem::path ipaPath = std::filesystem::u8path(path.UTF8String ?: "");
        if (!loadIpaExecutable(ipaPath, ipa)) {
            return @{ @"ok": @NO, @"error": ToNSString(ipa.error) };
        }

        MachOImage image;
        std::string machoError;
        if (!loadMachOImage(ipa.executablePath, image, machoError)) {
            return @{ @"ok": @NO, @"error": ToNSString(machoError),
                      @"executable": ToNSString(ipa.executableName) };
        }

        if (image.bigEndian) {
            return @{ @"ok": @NO,
                      @"error": @"Big-endian ARM binaries are not supported by this interpreter.",
                      @"executable": ToNSString(ipa.executableName) };
        }

        try {
            Armv6 cpu;
            cpu.loadImage(image);
            const ArmRunReport run = cpu.runContinueOnFault(100000);
            NSMutableArray<NSString *> *faults = [NSMutableArray array];
            for (const auto& fault : run.faultMessages) {
                [faults addObject:ToNSString(fault)];
            }
            return @{
                @"ok": @YES,
                @"bundle": ToNSString(ipa.appBundlePath),
                @"executable": ToNSString(ipa.executableName),
                @"bytes": @(ipa.executableSize),
                @"segments": @(image.segments.size()),
                @"mappedBytes": @(image.totalMappedBytes),
                @"attempted": @(run.attempted),
                @"completed": @(run.completed),
                @"faultCount": @(run.faults),
                @"faults": faults,
                @"faultsTruncated": @(run.faults > faults.count),
                @"halted": @(run.halted),
                @"fetchFault": @(run.stoppedOnFetchFault),
                @"pc": [NSString stringWithFormat:@"0x%08X", cpu.reg(15)]
            };
        } catch (const std::exception& error) {
            return @{ @"ok": @NO, @"error": ToNSString(error.what()),
                      @"executable": ToNSString(ipa.executableName) };
        }
    }
}
@end
