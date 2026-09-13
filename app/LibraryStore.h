#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/* A library of user-supplied files, not a package installer. It neither loads
 * native code nor claims that imported APKs are runnable. */
@interface ARTBoxLibraryStore : NSObject
@property(nonatomic, readonly) NSURL *root;
- (instancetype)initWithRoot:(NSURL *)root;
- (NSArray<NSDictionary<NSString *, NSString *> *> *)entriesWithError:(NSError **)error;
- (nullable NSDictionary<NSString *, NSString *> *)importAPK:(NSURL *)url error:(NSError **)error;
- (BOOL)removeEntry:(NSString *)identifier error:(NSError **)error;
@end

NS_ASSUME_NONNULL_END
