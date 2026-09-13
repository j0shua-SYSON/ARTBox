#import "ConsoleViewController.h"

#include "artbox/runtime.h"
#if ARTBOX_M1
#include "artbox/native_hello.h"
#include "guest_image.h"
#endif

@interface ConsoleViewController ()
@property(nonatomic, strong) UITextView *console;
@property(nonatomic) BOOL runHello;
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

- (instancetype)initWithRunHello:(BOOL)runHello {
    self = [super initWithNibName:nil bundle:nil];
    if (self) _runHello = runHello;
    return self;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = self.runHello ? @"Hello" : @"Runtime log";
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
#if ARTBOX_M1
    if (!self.runHello) return;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
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
#else
    if (self.runHello) [self appendMessage:@"This build does not include the Hello demo. Install the ARTBox IPA to run it."];
#endif
}

- (void)appendMessage:(NSString *)message {
    self.console.text = [self.console.text stringByAppendingFormat:@"%@\n", message];
    [self.console scrollRangeToVisible:NSMakeRange(self.console.text.length, 0)];
}

@end
