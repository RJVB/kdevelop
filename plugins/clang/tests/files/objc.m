
/// "type" : { "toString" : "NSString" },
/// "useCount" : 2
@interface NSString
{
}
@end

/// "identifiedTypeDeclaration" : { "isDefinition" : false }
NSString* f;

/// "type" : { "toString" : "NSString*" }
NSString *strn = @"NSString or CFString";

#ifndef __OBJC__
NSString *strn2 = "C String";  // in ObjC this would raise the error "String literal must be prefixed by '@'"
#endif
