#import <UIKit/UIKit.h>
#import "ConsoleViewController.h"

@interface ARTBoxSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

@implementation ARTBoxSceneDelegate
- (void)scene:(UIScene *)scene
    willConnectToSession:(UISceneSession *)session
    options:(UISceneConnectionOptions *)options {
    (void)session;
    (void)options;
    if (![scene isKindOfClass:UIWindowScene.class]) {
        return;
    }
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    ConsoleViewController *console = [[ConsoleViewController alloc] init];
    self.window.rootViewController = [[UINavigationController alloc]
        initWithRootViewController:console];
    [self.window makeKeyAndVisible];
}
@end

@interface ARTBoxAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation ARTBoxAppDelegate
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(ARTBoxAppDelegate.class));
    }
}
