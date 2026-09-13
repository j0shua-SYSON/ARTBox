#import "LauncherViewController.h"
#import "LibraryStore.h"
#import "ConsoleViewController.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <math.h>

@interface ARTBoxAppCell : UICollectionViewCell
@property(nonatomic, strong) UIImageView *icon;
@property(nonatomic, strong) UILabel *name;
@property(nonatomic, strong) UILabel *detail;
- (void)configure:(NSDictionary *)entry;
@end

@implementation ARTBoxAppCell
- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    self.icon = [[UIImageView alloc] init];
    self.icon.contentMode = UIViewContentModeCenter;
    self.icon.layer.cornerRadius = 18;
    self.icon.backgroundColor = [UIColor.systemBlueColor colorWithAlphaComponent:0.10];
    self.icon.tintColor = UIColor.systemBlueColor;
    self.icon.translatesAutoresizingMaskIntoConstraints = NO;
    self.name = [[UILabel alloc] init];
    self.name.font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
    self.name.adjustsFontForContentSizeCategory = YES;
    self.name.numberOfLines = 2;
    self.name.textAlignment = NSTextAlignmentCenter;
    self.detail = [[UILabel alloc] init];
    self.detail.font = [UIFont preferredFontForTextStyle:UIFontTextStyleCaption1];
    self.detail.adjustsFontForContentSizeCategory = YES;
    self.detail.textColor = UIColor.secondaryLabelColor;
    self.detail.textAlignment = NSTextAlignmentCenter;
    self.detail.numberOfLines = 0;
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[self.icon, self.name, self.detail]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.alignment = UIStackViewAlignmentCenter;
    stack.spacing = 5;
    [stack setCustomSpacing:10 afterView:self.icon];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [self.contentView addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [self.icon.widthAnchor constraintEqualToConstant:68],
        [self.icon.heightAnchor constraintEqualToConstant:68],
        [stack.topAnchor constraintEqualToAnchor:self.contentView.topAnchor constant:6],
        [stack.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor constant:4],
        [stack.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor constant:-4],
        [self.name.widthAnchor constraintEqualToAnchor:stack.widthAnchor],
        [self.detail.widthAnchor constraintEqualToAnchor:stack.widthAnchor]
    ]];
    self.isAccessibilityElement = YES;
    self.accessibilityTraits = UIAccessibilityTraitButton;
    self.icon.isAccessibilityElement = NO;
    self.name.isAccessibilityElement = NO;
    self.detail.isAccessibilityElement = NO;
    return self;
}

- (void)configure:(NSDictionary *)entry {
    BOOL demo = [entry[@"id"] isEqual:@"hello"];
    self.name.text = entry[@"name"];
    self.detail.text = demo ? @"Demo" : @"Imported";
    UIImageSymbolConfiguration *symbol = [UIImageSymbolConfiguration configurationWithPointSize:29 weight:UIImageSymbolWeightMedium];
    self.icon.image = [UIImage systemImageNamed:demo ? @"terminal" : @"shippingbox" withConfiguration:symbol];
    self.accessibilityIdentifier = [@"artbox.app." stringByAppendingString:entry[@"id"]];
    self.accessibilityLabel = [NSString stringWithFormat:@"%@, %@", self.name.text, self.detail.text];
    self.accessibilityHint = demo ? @"Runs the built-in Hello demo" : @"Shows details. Launching imported APKs is not available yet.";
}

- (void)setHighlighted:(BOOL)highlighted {
    [super setHighlighted:highlighted];
    self.contentView.alpha = highlighted ? 0.55 : 1.0;
}
@end

@interface ARTBoxLauncherViewController () <UICollectionViewDataSource, UICollectionViewDelegateFlowLayout,
    UISearchResultsUpdating, UIDocumentPickerDelegate>
@property(nonatomic, strong) UICollectionView *collection;
@property(nonatomic, strong) UISearchController *search;
@property(nonatomic, strong) UILabel *countLabel;
@property(nonatomic, strong) UILabel *message;
@property(nonatomic, strong) ARTBoxLibraryStore *store;
@property(nonatomic, copy) NSArray<NSDictionary *> *entries;
@property(nonatomic, copy) NSArray<NSDictionary *> *visibleEntries;
@property(nonatomic) BOOL importing;
@end

@implementation ARTBoxLauncherViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"ARTBox";
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    self.navigationController.navigationBar.prefersLargeTitles = YES;
    UIFontDescriptor *rounded = [[UIFont preferredFontForTextStyle:UIFontTextStyleLargeTitle].fontDescriptor
        fontDescriptorWithDesign:UIFontDescriptorSystemDesignRounded];
    self.navigationController.navigationBar.largeTitleTextAttributes = @{
        NSFontAttributeName: [UIFont fontWithDescriptor:rounded size:0]};
    UIBarButtonItem *add = [[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"plus"]
        style:UIBarButtonItemStylePlain target:self action:@selector(importApp)];
    add.accessibilityLabel = @"Import APK";
    add.accessibilityIdentifier = @"artbox.import";
    UIBarButtonItem *logs = [[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"text.alignleft"]
        style:UIBarButtonItemStylePlain target:self action:@selector(showDiagnostics)];
    logs.accessibilityLabel = @"Runtime log";
    logs.accessibilityIdentifier = @"artbox.diagnostics";
    self.navigationItem.rightBarButtonItems = @[add, logs];
    self.search = [[UISearchController alloc] initWithSearchResultsController:nil];
    self.search.searchResultsUpdater = self;
    self.search.obscuresBackgroundDuringPresentation = NO;
    self.search.searchBar.placeholder = @"Search apps";
    self.search.searchBar.accessibilityIdentifier = @"artbox.search";
    self.navigationItem.searchController = self.search;
    self.navigationItem.hidesSearchBarWhenScrolling = NO;
    self.definesPresentationContext = YES;

    UILabel *heading = [[UILabel alloc] init];
    heading.text = @"Apps";
    heading.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle2];
    heading.adjustsFontForContentSizeCategory = YES;
    heading.accessibilityTraits = UIAccessibilityTraitHeader;
    self.countLabel = [[UILabel alloc] init];
    self.countLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
    self.countLabel.adjustsFontForContentSizeCategory = YES;
    self.countLabel.textColor = UIColor.secondaryLabelColor;
    self.countLabel.accessibilityIdentifier = @"artbox.app-count";
    UIStackView *headingRow = [[UIStackView alloc] initWithArrangedSubviews:@[heading, self.countLabel]];
    headingRow.alignment = UIStackViewAlignmentFirstBaseline;
    headingRow.spacing = 12;
    headingRow.translatesAutoresizingMaskIntoConstraints = NO;
    [heading setContentHuggingPriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];
    [self.countLabel setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    [self.view addSubview:headingRow];

    UICollectionViewFlowLayout *layout = [[UICollectionViewFlowLayout alloc] init];
    layout.minimumInteritemSpacing = 8;
    layout.minimumLineSpacing = 18;
    layout.sectionInset = UIEdgeInsetsMake(16, 16, 28, 16);
    self.collection = [[UICollectionView alloc] initWithFrame:CGRectZero collectionViewLayout:layout];
    self.collection.backgroundColor = UIColor.systemBackgroundColor;
    self.collection.alwaysBounceVertical = YES;
    self.collection.keyboardDismissMode = UIScrollViewKeyboardDismissModeOnDrag;
    self.collection.dataSource = self;
    self.collection.delegate = self;
    self.collection.accessibilityIdentifier = @"artbox.library";
    self.collection.translatesAutoresizingMaskIntoConstraints = NO;
    [self.collection registerClass:ARTBoxAppCell.class forCellWithReuseIdentifier:@"app"];
    [self.view addSubview:self.collection];
    self.message = [[UILabel alloc] init];
    self.message.textColor = UIColor.secondaryLabelColor;
    self.message.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    self.message.adjustsFontForContentSizeCategory = YES;
    self.message.numberOfLines = 0;
    self.message.translatesAutoresizingMaskIntoConstraints = NO;
    self.message.accessibilityIdentifier = @"artbox.library-help";
    [self.view addSubview:self.message];
    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [headingRow.topAnchor constraintEqualToAnchor:safe.topAnchor constant:16],
        [headingRow.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:24],
        [headingRow.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-24],
        [self.collection.topAnchor constraintEqualToAnchor:headingRow.bottomAnchor constant:4],
        [self.collection.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [self.collection.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [self.collection.bottomAnchor constraintEqualToAnchor:self.message.topAnchor constant:-12],
        [self.message.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:24],
        [self.message.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-24],
        [self.message.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-16]
    ]];
    NSURL *support = [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
    self.store = [[ARTBoxLibraryStore alloc] initWithRoot:[support URLByAppendingPathComponent:@"ARTBox/Library" isDirectory:YES]];
    [self reloadLibrary];
}

- (void)showError:(NSError *)error {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Library unavailable"
        message:error.localizedDescription preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)reloadLibrary {
    NSError *error = nil;
    NSArray *imported = [self.store entriesWithError:&error];
    self.entries = [@[@{@"id": @"hello", @"name": @"Hello"}] arrayByAddingObjectsFromArray:imported];
    [self updateSearchResultsForSearchController:self.search];
    if (error) dispatch_async(dispatch_get_main_queue(), ^{ [self showError:error]; });
}

- (void)updateSearchResultsForSearchController:(UISearchController *)searchController {
    NSString *query = [searchController.searchBar.text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    self.visibleEntries = query.length == 0 ? self.entries : [self.entries filteredArrayUsingPredicate:
        [NSPredicate predicateWithBlock:^BOOL(NSDictionary *entry, NSDictionary *bindings) {
            (void)bindings;
            return [entry[@"name"] localizedStandardContainsString:query];
        }]];
    self.countLabel.text = [NSString stringWithFormat:@"%lu", (unsigned long)self.visibleEntries.count];
    self.countLabel.accessibilityLabel = [NSString stringWithFormat:@"%lu apps", (unsigned long)self.visibleEntries.count];
    self.message.text = self.visibleEntries.count == 0 ? @"No apps match your search." :
        @"Try Hello, or use + to add an APK. Imported APKs can be saved here; launching them is not available yet.";
    [self.collection reloadData];
}

- (NSInteger)collectionView:(UICollectionView *)collectionView numberOfItemsInSection:(NSInteger)section {
    (void)collectionView; (void)section;
    return (NSInteger)self.visibleEntries.count;
}

- (__kindof UICollectionViewCell *)collectionView:(UICollectionView *)collectionView cellForItemAtIndexPath:(NSIndexPath *)indexPath {
    ARTBoxAppCell *cell = [collectionView dequeueReusableCellWithReuseIdentifier:@"app" forIndexPath:indexPath];
    [cell configure:self.visibleEntries[(NSUInteger)indexPath.item]];
    return cell;
}

- (CGSize)collectionView:(UICollectionView *)collectionView layout:(UICollectionViewLayout *)layout sizeForItemAtIndexPath:(NSIndexPath *)indexPath {
    (void)layout; (void)indexPath;
    CGFloat width = MAX(1, collectionView.bounds.size.width - 32);
    BOOL large = UIContentSizeCategoryIsAccessibilityCategory(self.traitCollection.preferredContentSizeCategory);
    NSInteger columns = MAX(1, MIN(6, (NSInteger)((width + 8) / (large ? 188 : 108))));
    CGFloat height = 94 + [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline].lineHeight * 2 +
        [UIFont preferredFontForTextStyle:UIFontTextStyleCaption1].lineHeight * 2;
    return CGSizeMake(floor((width - 8 * (columns - 1)) / columns), ceil(height));
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self.collection.collectionViewLayout invalidateLayout];
}

- (void)showDiagnostics {
    ConsoleViewController *console = [[ConsoleViewController alloc] initWithRunHello:NO];
    console.navigationItem.largeTitleDisplayMode = UINavigationItemLargeTitleDisplayModeNever;
    [self.navigationController pushViewController:console animated:YES];
}

- (void)collectionView:(UICollectionView *)collectionView didSelectItemAtIndexPath:(NSIndexPath *)indexPath {
    [collectionView deselectItemAtIndexPath:indexPath animated:YES];
    NSDictionary *entry = self.visibleEntries[(NSUInteger)indexPath.item];
    if ([entry[@"id"] isEqual:@"hello"]) {
        ConsoleViewController *hello = [[ConsoleViewController alloc] initWithRunHello:YES];
        hello.navigationItem.largeTitleDisplayMode = UINavigationItemLargeTitleDisplayModeNever;
        [self.navigationController pushViewController:hello animated:YES];
        return;
    }
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:entry[@"name"]
        message:@"This APK is saved in your library. ARTBox cannot launch imported APKs yet."
        preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Remove from library" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *action) {
        (void)action;
        NSError *error = nil;
        if (![self.store removeEntry:entry[@"id"] error:&error]) [self showError:error];
        else [self reloadLibrary];
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)importApp {
    if (self.importing) return;
    UTType *apk = [UTType typeWithFilenameExtension:@"apk" conformingToType:UTTypeData];
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[apk ? apk : UTTypeData] asCopy:YES];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    (void)controller;
    if (urls.count == 0 || self.importing) return;
    self.importing = YES;
    self.navigationItem.rightBarButtonItems.firstObject.enabled = NO;
    NSURL *url = urls.firstObject;
    BOOL scoped = [url startAccessingSecurityScopedResource];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSError *error = nil;
        [self.store importAPK:url error:&error];
        if (scoped) [url stopAccessingSecurityScopedResource];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.importing = NO;
            self.navigationItem.rightBarButtonItems.firstObject.enabled = YES;
            if (error) [self showError:error];
            else [self reloadLibrary];
        });
    });
}
@end
