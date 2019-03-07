/**
 * "identifier" : "main",
 * "internalContext" : { "type" : "Function", "range" : "[(5, 0), (8, 1)]" },
 * "range" : "[(5, 4), (5, 8)]"
 */
int main()
{
    return 0;
}

/**
 * "identifier" : "bar",
 * "internalContext" : { "type" : "Namespace", "range" : "[(15, 0), (93, 1)]" },
 * "kind" : "Namespace"
 */
namespace bar
{
    /**
     * "identifier" : "foo",
     * "internalContext" : { "null" : false, "range" : "[(21, 4), (21, 14)]" }
     */
    void foo();

    /**
     * "identifier" : "foostruct",
     * "internalContext" : { "type" : "Class", "range" : "[(27, 4), (92, 5)]" }
     */
    struct foostruct
    {
        /**
         * "identifier" : "foostruct",
         * "internalContext" : { "type" : "Function", "range" : "[(33, 8), (35, 9)]" }
         */
        foostruct()
        {
        }

        /**
         * "identifier" : "~foostruct", "range" : "[(41, 8), (41, 18)]",
         * "internalContext" : { "type" : "Function", "range" : "[(41, 8), (43, 9)]" }
         */
        ~foostruct()
        {
        }

        /**
         * "identifier" : "operator int",
         * "internalContext" : { "type" : "Function", "range" : "[(49, 8), (52, 9)]" }
         */
        operator int()
        {
            return 0;
        }

        /**
         * "identifier" : "templateFunction",
         * "internalContext" : { "type" : "Function", "range" : "[(58, 8), (61, 9)]" }
         */
        template<class T>
        void templateFunction()
        {
        }

        /**
         * "identifier" : "fooenum",
         * "internalContext" : { "type" : "Enum", "range" : "[(67, 8), (69, 9)]" }
         */
        enum fooenum
        {
        };

        /**
         * "identifier" : "foomethod",
         * "internalContext" : { "type" : "Function", "range" : "[(75, 8), (75, 23)]" }
         */
        int foomethod();

        /**
         * "identifier" : "deffoomethod",
         * "internalContext" : { "type" : "Function", "range" : "[(81, 8), (83, 9)]" }
         */
        void deffoomethod()
        {
        }

        /**
         * "identifier" : "operator=", "range" : "[(89, 19), (89, 28)]",
         * "internalContext" : { "type" : "Function", "range" : "[(89, 8), (91, 9)]" }
         */
        foostruct& operator=(const foostruct& other)
        { return *this;
        }
    };
}

/// "identifier" : "", "range" : "[(96, 0), (96, 0)]"
struct {} unnamedStructObject;

/// "identifier" : "", "range" : "[(99, 0), (99, 0)]"
union {} unnamedUnionObject;

/// "type" : { "toString" : "NSString" },
/// "useCount" : 1
@class NSString;

/// "type" : { "toString" : "NSString*" }
NSString *strn = @"NSString or CFString";

/// "type" : { "toString" : "NSString*" }
auto b = @"Another string";



