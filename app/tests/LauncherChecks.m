/* Simulator-only UIKit integration checks; never included in the device app.
 * Actions use UIKit delegates/controls. This is not physical touch evidence. */
#import <UIKit/UIKit.h>
#import "LauncherViewController.h"
#import "LibraryStore.h"
#include <stdlib.h>

static UIWindow *testWindow;
static NSURL *evidence;
static NSUInteger checks;

static void require(BOOL condition, NSString *message) {
    ++checks;
    if (condition) return;
    NSData *report = [NSJSONSerialization dataWithJSONObject:@{@"passed": @NO, @"failure": message, @"checks": @(checks)} options:0 error:nil];
    [report writeToURL:[evidence URLByAppendingPathComponent:@"result.json"] atomically:YES];
    NSLog(@"ARTBox launcher check failed: %@", message);
    exit(1);
}

static UIView *find(UIView *view, NSString *identifier) {
    if ([view.accessibilityIdentifier isEqual:identifier]) return view;
    for (UIView *child in view.subviews) {
        UIView *found = find(child, identifier);
        if (found) return found;
    }
    return nil;
}

static void snapshot(NSString *name) {
    [testWindow layoutIfNeeded];
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:testWindow.bounds];
    NSData *png = [renderer PNGDataWithActions:^(UIGraphicsImageRendererContext *context) {
        (void)context;
        [testWindow drawViewHierarchyInRect:testWindow.bounds afterScreenUpdates:YES];
    }];
    require([png writeToURL:[evidence URLByAppendingPathComponent:name] atomically:YES], @"write screenshot");
}

static void later(void (^action)(void)) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC), dispatch_get_main_queue(), action);
}

static void checkLayout(UICollectionView *grid) {
    [testWindow layoutIfNeeded];
    require(grid.bounds.size.width > 0 && grid.bounds.size.height > 0, @"grid has visible area");
    for (UICollectionViewCell *cell in grid.visibleCells) {
        require(!cell.contentView.hasAmbiguousLayout, @"app cell constraints resolve");
        for (UIView *stack in cell.contentView.subviews) {
            require(CGRectGetMaxY(stack.frame) <= CGRectGetHeight(cell.bounds) + 1, @"cell content fits vertically");
            require(CGRectGetMaxX(stack.frame) <= CGRectGetWidth(cell.bounds) + 1, @"cell content fits horizontally");
        }
    }
}

static void finishChecks(UINavigationController *navigation, UIViewController *launcher) {
    UICollectionView *grid = (UICollectionView *)find(launcher.view, @"artbox.library");
    require([grid numberOfItemsInSection:0] == 2, @"imported file appears in library");
    checkLayout(grid);
    snapshot(@"library.png");
    /* Rebuild the controller to check that it reads the saved library. */
    ARTBoxLauncherViewController *reopened = [[ARTBoxLauncherViewController alloc] init];
    [navigation setViewControllers:@[reopened] animated:NO];
    later(^{
        UICollectionView *saved = (UICollectionView *)find(reopened.view, @"artbox.library");
        require([saved numberOfItemsInSection:0] == 2, @"library survives controller recreation");
        UISearchController *search = reopened.navigationItem.searchController;
        search.searchBar.text = @"UI demo";
        [search.searchResultsUpdater updateSearchResultsForSearchController:search];
        require([saved numberOfItemsInSection:0] == 1, @"search finds imported app");
        [saved.delegate collectionView:saved didSelectItemAtIndexPath:[NSIndexPath indexPathForItem:0 inSection:0]];
        later(^{
            UIAlertController *details = (UIAlertController *)reopened.presentedViewController;
            require([details isKindOfClass:UIAlertController.class] && [details.message containsString:@"cannot launch"], @"import status does not imply execution");
            [details dismissViewControllerAnimated:NO completion:^{
                search.searchBar.text = @"";
                [search.searchResultsUpdater updateSearchResultsForSearchController:search];
                testWindow.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
                later(^{
                    checkLayout(saved);
                    snapshot(@"library-dark.png");
                    testWindow.overrideUserInterfaceStyle = UIUserInterfaceStyleLight;
                    [navigation setOverrideTraitCollection:[UITraitCollection traitCollectionWithPreferredContentSizeCategory:
                        UIContentSizeCategoryAccessibilityExtraExtraExtraLarge] forChildViewController:reopened];
                    later(^{
                        checkLayout(saved);
                        snapshot(@"library-accessibility.png");
                        NSDictionary *record = @{@"passed": @YES, @"checks": @(checks),
                            @"scope": @"Simulator UIKit actions and layout; no physical touch or Android runtime execution",
                            @"screen_width": @(testWindow.bounds.size.width), @"screen_height": @(testWindow.bounds.size.height)};
                        [[NSJSONSerialization dataWithJSONObject:record options:0 error:nil]
                            writeToURL:[evidence URLByAppendingPathComponent:@"result.json"] atomically:YES];
                    });
                });
            }];
        });
    });
}

static void waitForImport(UINavigationController *navigation, UIViewController *launcher, unsigned remaining) {
    UICollectionView *grid = (UICollectionView *)find(launcher.view, @"artbox.library");
    if ([grid numberOfItemsInSection:0] == 2) { finishChecks(navigation, launcher); return; }
    require(remaining > 0, @"import completes");
    later(^{ waitForImport(navigation, launcher, remaining - 1); });
}

void ARTBoxCheckLauncher(UIWindow *window) {
    testWindow = window;
    NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    evidence = [documents URLByAppendingPathComponent:@"LauncherChecks" isDirectory:YES];
    [NSFileManager.defaultManager createDirectoryAtURL:evidence withIntermediateDirectories:YES attributes:nil error:nil];
    [UIView setAnimationsEnabled:NO];
    UINavigationController *navigation = (UINavigationController *)window.rootViewController;
    UIViewController *launcher = navigation.topViewController;
    later(^{
        require([launcher isKindOfClass:ARTBoxLauncherViewController.class], @"launcher is the initial screen");
        UICollectionView *grid = (UICollectionView *)find(launcher.view, @"artbox.library");
        require([grid isKindOfClass:UICollectionView.class] && [grid numberOfItemsInSection:0] == 1, @"fresh library contains Hello");
        checkLayout(grid);
        snapshot(@"launcher.png");
        UISearchController *search = launcher.navigationItem.searchController;
        search.searchBar.text = @"does-not-exist";
        [search.searchResultsUpdater updateSearchResultsForSearchController:search];
        require([grid numberOfItemsInSection:0] == 0, @"unmatched search is empty");
        search.searchBar.text = @"hello";
        [search.searchResultsUpdater updateSearchResultsForSearchController:search];
        require([grid numberOfItemsInSection:0] == 1, @"search is case-insensitive");
        search.searchBar.text = @"";
        [search.searchResultsUpdater updateSearchResultsForSearchController:search];
        [grid.delegate collectionView:grid didSelectItemAtIndexPath:[NSIndexPath indexPathForItem:0 inSection:0]];
        later(^{
            UITextView *console = (UITextView *)find(navigation.topViewController.view, @"artbox.log");
            require([navigation.topViewController.title isEqual:@"Hello"] && [console.text containsString:@"ARTBox ready"], @"Hello opens its runtime view");
            snapshot(@"hello.png");
            [navigation popToRootViewControllerAnimated:NO];
            UIBarButtonItem *logs = launcher.navigationItem.rightBarButtonItems.lastObject;
            [UIApplication.sharedApplication sendAction:logs.action to:logs.target from:logs forEvent:nil];
            later(^{
                require([navigation.topViewController.title isEqual:@"Runtime log"], @"diagnostics have their own view");
                [navigation popToRootViewControllerAnimated:NO];
                UIBarButtonItem *add = launcher.navigationItem.rightBarButtonItems.firstObject;
                [UIApplication.sharedApplication sendAction:add.action to:add.target from:add forEvent:nil];
                later(^{
                    UIDocumentPickerViewController *picker = (UIDocumentPickerViewController *)launcher.presentedViewController;
                    require([picker isKindOfClass:UIDocumentPickerViewController.class], @"import opens the system file picker");
                    [picker dismissViewControllerAnimated:NO completion:^{
                        NSURL *input = [evidence URLByAppendingPathComponent:@"UI demo.apk"];
                        const unsigned char bytes[] = {'P', 'K', 3, 4, 0, 0};
                        [[NSData dataWithBytes:bytes length:sizeof(bytes)] writeToURL:input atomically:YES];
                        [(id<UIDocumentPickerDelegate>)launcher documentPicker:picker didPickDocumentsAtURLs:@[input]];
                        waitForImport(navigation, launcher, 40);
                    }];
                });
            });
        });
    });
}
