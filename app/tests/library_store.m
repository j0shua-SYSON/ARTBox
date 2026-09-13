#import <Foundation/Foundation.h>
#import "LibraryStore.h"

#define CHECK(value) do { if (!(value)) { NSLog(@"Failed line %d: %s", __LINE__, #value); return 1; } } while (0)

int main(int argc, const char **argv) {
    @autoreleasepool {
        CHECK(argc == 2);
        NSURL *root = [NSURL fileURLWithPath:@(argv[1]) isDirectory:YES];
        NSFileManager *fm = NSFileManager.defaultManager;
        NSError *error = nil;
        CHECK([fm createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:&error]);
        NSURL *library = [root URLByAppendingPathComponent:@"library" isDirectory:YES];
        ARTBoxLibraryStore *store = [[ARTBoxLibraryStore alloc] initWithRoot:library];
        CHECK([store entriesWithError:&error].count == 0 && !error);
        /* Deliberately just a ZIP signature: these test file cataloguing, not
         * APK manifest/signature validation or Android installation. */
        const unsigned char bytes[] = {'P', 'K', 3, 4, 1, 2, 3, 4};
        NSData *data = [NSData dataWithBytes:bytes length:sizeof(bytes)];
        NSURL *source = [root URLByAppendingPathComponent:@"My demo.APK"];
        CHECK([data writeToURL:source options:NSDataWritingAtomic error:&error]);
        NSDictionary *first = [store importAPK:source error:&error];
        CHECK(first && !error && [first[@"name"] isEqual:@"My demo"]);
        NSDictionary *second = [store importAPK:source error:&error];
        CHECK(second && ![first[@"id"] isEqual:second[@"id"]]);
        CHECK([store entriesWithError:&error].count == 2);
        CHECK([fm removeItemAtURL:source error:&error]);
        store = [[ARTBoxLibraryStore alloc] initWithRoot:library];
        CHECK([store entriesWithError:&error].count == 2);
        NSURL *copied = [[library URLByAppendingPathComponent:first[@"id"]] URLByAppendingPathComponent:@"package.apk"];
        CHECK([[NSData dataWithContentsOfURL:copied] isEqual:data]);
        NSURL *invalid = [root URLByAppendingPathComponent:@"broken.apk"];
        CHECK([[@"not an APK" dataUsingEncoding:NSUTF8StringEncoding] writeToURL:invalid options:0 error:&error]);
        error = nil;
        CHECK(![store importAPK:invalid error:&error] && error);
        error = nil;
        CHECK(![store importAPK:root error:&error] && error);
        error = nil;
        CHECK(![store removeEntry:@"../broken.apk" error:&error] && error);
        CHECK([fm fileExistsAtPath:invalid.path]);
        error = nil;
        CHECK([store removeEntry:first[@"id"] error:&error] && !error);
        CHECK([store entriesWithError:&error].count == 1);
        CHECK(![fm fileExistsAtPath:copied.path]);
        CHECK([store removeEntry:second[@"id"] error:&error]);
        CHECK([store entriesWithError:&error].count == 0);
        NSLog(@"ARTBox library: import, persistence, duplicate names, validation and removal passed");
    }
    return 0;
}
