#import <UIKit/UIKit.h>
#import "ConsoleViewController.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static void openLaunchLog(void) {
    // An early harness _Exit does not generate a crash report or drain the UI queue.
    // Keep this launch's diagnostics inside the ordinary app container instead.
    NSFileManager *manager = NSFileManager.defaultManager;
    NSURL *cache = [manager URLForDirectory:NSCachesDirectory inDomain:NSUserDomainMask
                         appropriateForURL:nil create:YES error:NULL];
    NSURL *directory = [cache URLByAppendingPathComponent:@"ARTBox" isDirectory:YES];
    if (!directory || ![manager createDirectoryAtURL:directory
                        withIntermediateDirectories:YES attributes:nil error:NULL]) {
        return;
    }
    NSURL *log = [directory URLByAppendingPathComponent:@"launch.log"];
    int descriptor = open(log.fileSystemRepresentation, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (descriptor < 0) return;
    (void)dup2(descriptor, STDERR_FILENO);
    (void)dup2(descriptor, STDOUT_FILENO);
    if (descriptor != STDERR_FILENO && descriptor != STDOUT_FILENO) close(descriptor);
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    fprintf(stderr, "ARTBox launch\n");
}

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
        openLaunchLog();
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(ARTBoxAppDelegate.class));
    }
}
