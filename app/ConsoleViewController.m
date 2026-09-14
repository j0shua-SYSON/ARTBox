#import "ConsoleViewController.h"

#include "artbox/runtime.h"
#include <string.h>
#if ARTBOX_M1
#include "artbox/native_hello.h"
#include "guest_image.h"
#endif
#if ARTBOX_M2
#include "artbox/native_bionic.h"
#endif

@interface ConsoleViewController ()
@property(nonatomic, strong) UITextView *console;
- (void)appendMessage:(NSString *)message;
@end

static void app_log(void *context, const char *message, size_t length) {
    ConsoleViewController *controller = (__bridge ConsoleViewController *)context;
    NSString *copy = [[NSString alloc] initWithBytes:message
                                            length:length
                                          encoding:NSUTF8StringEncoding];
    if (copy == nil) {
        copy = @"[invalid UTF-8 log message]";
    }
    if ([NSThread isMainThread]) {
        [controller appendMessage:copy];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [controller appendMessage:copy];
        });
    }
}

@implementation ConsoleViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"ARTBox";
    self.view.backgroundColor = UIColor.systemBackgroundColor;

    self.console = [[UITextView alloc] initWithFrame:CGRectZero];
    self.console.translatesAutoresizingMaskIntoConstraints = NO;
    self.console.editable = NO;
    self.console.selectable = YES;
    self.console.alwaysBounceVertical = YES;
    self.console.backgroundColor = UIColor.secondarySystemBackgroundColor;
    self.console.textColor = UIColor.labelColor;
    self.console.font = [[UIFontMetrics metricsForTextStyle:UIFontTextStyleBody]
        scaledFontForFont:[UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightRegular]];
    self.console.adjustsFontForContentSizeCategory = YES;
    self.console.textContainerInset = UIEdgeInsetsMake(16, 12, 16, 12);
    self.console.accessibilityIdentifier = @"artbox.log";
    self.console.accessibilityLabel = @"Runtime log";
    [self.view addSubview:self.console];

    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [self.console.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [self.console.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [self.console.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [self.console.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor]
    ]];

    const artbox_host host = {app_log, (__bridge void *)self};
    if (artbox_start(&host) != ARTBOX_OK) {
        [self appendMessage:@"ARTBox startup failed"];
    }
#if ARTBOX_M1 || ARTBOX_M2
    dispatch_queue_t runtimeQueue = dispatch_queue_create("org.artbox.acceptance", DISPATCH_QUEUE_SERIAL);
#endif
#if ARTBOX_M1
    dispatch_async(runtimeQueue, ^{
        const artbox_host guestHost = {app_log, (__bridge void *)self};
        NSString *frameworks = NSBundle.mainBundle.privateFrameworksPath;
        for (NSString *kind in @[@"Converted", @"Wrapped"]) {
            NSString *name = [@"ARTBox" stringByAppendingString:kind];
            NSString *library = [[frameworks stringByAppendingPathComponent:
                [name stringByAppendingString:@".framework"]] stringByAppendingPathComponent:name];
            NSString *starting = [NSString stringWithFormat:@"%@: starting Android hello", kind];
            app_log((__bridge void *)self, starting.UTF8String, [starting lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
            int status = artbox_run_native_hello(library.fileSystemRepresentation,
                                                ARTBOX_GUEST_IMAGE_SIZE, &guestHost);
            NSString *result = status == 0 ?
                [NSString stringWithFormat:@"%@: exit 0; five syscalls verified", kind] :
                [NSString stringWithFormat:@"%@: failed (%d)", kind, status];
            app_log((__bridge void *)self, result.UTF8String, [result lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
        }
    });
#endif
#if ARTBOX_M2
    dispatch_async(runtimeQueue, ^{
        const artbox_host guestHost = {app_log, (__bridge void *)self};
        NSFileManager *manager = NSFileManager.defaultManager;
        NSError *error = nil;
        NSURL *support = [manager URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask
                                appropriateForURL:nil create:YES error:&error];
        NSURL *root = [support URLByAppendingPathComponent:[@"ARTBoxM2-" stringByAppendingString:NSUUID.UUID.UUIDString]
                                              isDirectory:YES];
        for (NSString *directory in @[@"data", @"system"]) {
            if (!root || ![manager createDirectoryAtURL:[root URLByAppendingPathComponent:directory]
                              withIntermediateDirectories:YES attributes:nil error:&error]) {
                NSString *message = [NSString stringWithFormat:@"M2 storage setup failed: %@", error.localizedDescription];
                app_log((__bridge void *)self, message.UTF8String, [message lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
                return;
            }
        }
        NSArray<NSString *> *names = @[@"ARTBoxBionic", @"ARTBoxStartupClient", @"ARTBoxVersions", @"ARTBoxTLS"];
        NSArray<NSString *> *elfNames = @[@"libc.so", @"libstartup_client.so", @"libartbox_versions.so", @"libartbox_tls.so"];
        NSMutableArray<NSString *> *libraries = [NSMutableArray array], *elfs = [NSMutableArray array];
        artbox_bionic_input input = {{0}, {0}, root.fileSystemRepresentation, 0};
        for (unsigned i = 0; i < 4; ++i) {
            NSString *library = [[NSBundle.mainBundle.privateFrameworksPath stringByAppendingPathComponent:
                [names[i] stringByAppendingString:@".framework"]] stringByAppendingPathComponent:names[i]];
            NSString *elf = [[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"ARTBoxM2"]
                stringByAppendingPathComponent:elfNames[i]];
            [libraries addObject:library]; [elfs addObject:elf];
            input.frameworks[i] = libraries[i].fileSystemRepresentation;
            input.elfs[i] = elfs[i].fileSystemRepresentation;
        }
        const char *starting = "M2: starting Bionic threads, files, mappings and ELF TLS";
        app_log((__bridge void *)self, starting, strlen(starting));
        int result = artbox_run_native_bionic(&input, &guestHost);
        NSString *message = result == 0 ? @"M2: suite passed" : [NSString stringWithFormat:@"M2: failed (%d)", result];
        app_log((__bridge void *)self, message.UTF8String, [message lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
        if (result == 0) [manager removeItemAtURL:root error:NULL];
    });
#endif
}

- (void)appendMessage:(NSString *)message {
    self.console.text = [self.console.text stringByAppendingFormat:@"%@\n", message];
    [self.console scrollRangeToVisible:NSMakeRange(self.console.text.length, 0)];
}

@end
