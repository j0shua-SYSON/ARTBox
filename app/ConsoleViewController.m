#import "ConsoleViewController.h"

#include "artbox/runtime.h"

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
}

- (void)appendMessage:(NSString *)message {
    self.console.text = [self.console.text stringByAppendingFormat:@"%@\n", message];
    [self.console scrollRangeToVisible:NSMakeRange(self.console.text.length, 0)];
}

@end
