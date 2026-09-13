#import "LibraryStore.h"

static BOOL validIdentifier(NSString *identifier) {
    NSUUID *uuid = [[NSUUID alloc] initWithUUIDString:identifier];
    return uuid && [uuid.UUIDString caseInsensitiveCompare:identifier] == NSOrderedSame;
}

static void libraryError(NSError **error, NSString *message) {
    if (error) *error = [NSError errorWithDomain:@"org.artbox.library" code:1
        userInfo:@{NSLocalizedDescriptionKey: message}];
}

@implementation ARTBoxLibraryStore

- (instancetype)initWithRoot:(NSURL *)root {
    self = [super init];
    if (self) _root = [root copy];
    return self;
}

- (NSArray<NSDictionary<NSString *, NSString *> *> *)entriesWithError:(NSError **)error {
    NSFileManager *fm = NSFileManager.defaultManager;
    if (![fm createDirectoryAtURL:self.root withIntermediateDirectories:YES attributes:nil error:error]) return @[];
    NSArray<NSURL *> *folders = [fm contentsOfDirectoryAtURL:self.root
        includingPropertiesForKeys:@[NSURLIsDirectoryKey, NSURLIsSymbolicLinkKey]
        options:NSDirectoryEnumerationSkipsHiddenFiles error:error];
    if (!folders) return @[];
    NSMutableArray *entries = [NSMutableArray array];
    for (NSURL *folder in folders) {
        if (!validIdentifier(folder.lastPathComponent)) continue;
        NSNumber *directory = nil, *symlink = nil;
        if (![folder getResourceValue:&directory forKey:NSURLIsDirectoryKey error:error] ||
            ![folder getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:error]) return @[];
        if (!directory.boolValue || symlink.boolValue) continue;
        NSData *data = [NSData dataWithContentsOfURL:[folder URLByAppendingPathComponent:@"entry.json"]
                                           options:0 error:error];
        if (!data) return @[];
        id entry = [NSJSONSerialization JSONObjectWithData:data options:0 error:error];
        if (![entry isKindOfClass:NSDictionary.class] ||
            ![entry[@"id"] isEqual:folder.lastPathComponent] ||
            ![entry[@"name"] isKindOfClass:NSString.class] || [entry[@"name"] length] == 0 ||
            ![fm fileExistsAtPath:[folder URLByAppendingPathComponent:@"package.apk"].path]) {
            libraryError(error, @"An imported app's library record is damaged.");
            return @[];
        }
        [entries addObject:@{@"id": entry[@"id"], @"name": entry[@"name"]}];
    }
    [entries sortUsingComparator:^NSComparisonResult(NSDictionary *a, NSDictionary *b) {
        NSComparisonResult result = [a[@"name"] localizedStandardCompare:b[@"name"]];
        return result == NSOrderedSame ? [a[@"id"] compare:b[@"id"]] : result;
    }];
    return entries;
}

- (NSDictionary<NSString *, NSString *> *)importAPK:(NSURL *)url error:(NSError **)error {
    NSNumber *regular = nil;
    if (!url.isFileURL || [url.pathExtension caseInsensitiveCompare:@"apk"] != NSOrderedSame ||
        ![url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:error] || !regular.boolValue) {
        libraryError(error, @"Choose an APK file from Files.");
        return nil;
    }
    NSFileHandle *input = [NSFileHandle fileHandleForReadingFromURL:url error:error];
    if (!input) return nil;
    NSData *signature = [input readDataUpToLength:4 error:error];
    [input closeAndReturnError:nil];
    const unsigned char expected[] = {'P', 'K', 3, 4};
    if (![signature isEqual:[NSData dataWithBytes:expected length:sizeof(expected)]]) {
        libraryError(error, @"This file does not have an APK archive header.");
        return nil;
    }
    NSFileManager *fm = NSFileManager.defaultManager;
    if (![fm createDirectoryAtURL:self.root withIntermediateDirectories:YES attributes:nil error:error]) return nil;
    NSString *identifier = NSUUID.UUID.UUIDString;
    NSURL *staging = [self.root URLByAppendingPathComponent:[@".import-" stringByAppendingString:identifier] isDirectory:YES];
    NSURL *destination = [self.root URLByAppendingPathComponent:identifier isDirectory:YES];
    NSString *name = url.lastPathComponent.stringByDeletingPathExtension;
    if (name.length == 0) name = @"Imported app";
    NSDictionary *entry = @{@"id": identifier, @"name": name};
    NSData *metadata = [NSJSONSerialization dataWithJSONObject:entry options:0 error:error];
    if (!metadata) return nil;
    BOOL saved = [fm createDirectoryAtURL:staging withIntermediateDirectories:NO attributes:nil error:error] &&
        [fm copyItemAtURL:url toURL:[staging URLByAppendingPathComponent:@"package.apk"] error:error] &&
        [metadata writeToURL:[staging URLByAppendingPathComponent:@"entry.json"] options:NSDataWritingAtomic error:error] &&
        [fm moveItemAtURL:staging toURL:destination error:error];
    if (!saved) { [fm removeItemAtURL:staging error:nil]; return nil; }
    return entry;
}

- (BOOL)removeEntry:(NSString *)identifier error:(NSError **)error {
    if (!validIdentifier(identifier)) {
        libraryError(error, @"The app could not be found in this library.");
        return NO;
    }
    return [NSFileManager.defaultManager removeItemAtURL:
        [self.root URLByAppendingPathComponent:identifier isDirectory:YES] error:error];
}

@end
