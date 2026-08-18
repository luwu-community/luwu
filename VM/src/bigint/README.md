# bigint impl

Inspired by v8: https://chromium.googlesource.com/v8/v8.git/+/refs/heads/main/src/bigint/ (chefs kiss to them),

## Digit / RWDigit

The Digit abstraction enables for the actual algorithms to be separate from the internal low-level GC logic hence allowing for the actual VM layer (`lbigint`) to handle all the alloc/packing separately from the actual algorithms for bigint. This also means that the entire implementation should be more or less scoped to the `bigint` folder.

## Impl Notes

- All bigints are internally stored as a array of 64-bit 'limbs'. This enables for operations like Add/Sub/Mul/DivMod to operate directly on 64-bit chunks instead of digit by digit. 

## Add

Same long addition algorithm (add then carry to next and keep going), but on int64 'limbs'. After figurng out the max size between the two numbers, we can just do the same right to left column wise addition but with limbs, using the overflow to determine carries. Fairly straightforward (enough) algorithm

## ToString

The naive ToString algorithm on a bigint looks like this:

1. Divide by 10 (O(N^2) time)
2. Remainder of each division step is next decimal digit
3. Keep doing this from right to left

Unfortunately, dividing bigints like this is slow as it take O(num digits) divisions. To solve this, we can instead divide by `10**19` (largest power of 10 that fits in a `int64`). Because remainder will fit directly in a `int64`, we can just directly pull out 19 decimal digits at a time instead of 1 at a time. 

Note that dividing by 10^19 and extracting 19 digits at a time means that we perform 19 times less bigint divisions on average (num digits / 19). 

Finally, we then pad the result with leading zero's to ensure we have 19 digits e.g.

# FromString

The naive `FromString` algorithm is effectively the exact opposite/'inverse' of ToString and performs the following steps:

1. Multiply by 10
2. Add the value of next char
3. Keep doing this from left to right

Like with ToString, multiplying bigints is also O(N^2). We can once again solve this by batching our FromString into blocks of 19 characters (for the same reasoning as ToString dividing by `10**19`) which we can then convert into a 64 bit chunk (`chunk_val`). Finally, we can then merge the 64-bit chunk into the in-creation bigint and keep going.

Note that using chunks of 19 digits at a time means that we perform 19 times less bigint multiplications on average (num digits / 19). 
