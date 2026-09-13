#import <UIKit/UIKit.h>
#import "LauncherViewController.h"
#if ARTBOX_UI_TESTING
extern void ARTBoxCheckLauncher(UIWindow *window);
#endif

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
    ARTBoxLauncherViewController *launcher = [[ARTBoxLauncherViewController alloc] init];
    self.window.rootViewController = [[UINavigationController alloc]
        initWithRootViewController:launcher];
    [self.window makeKeyAndVisible];
#if ARTBOX_UI_TESTING
    ARTBoxCheckLauncher(self.window);
#endif
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
