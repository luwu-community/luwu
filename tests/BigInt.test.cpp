#include "doctest.h"
#include "lua.h"
#include "lualib.h"
#include "lobject.h"
#include <vector>

TEST_SUITE_BEGIN("BigIntTests");

TEST_CASE("BigInt_SMI_Arithmetic") {
    lua_State* L = luaL_newstate();
    
    TValue a, b, sum, diff, prod, div, mod;
    setbigintsmi(&a, 5, BigIntMode_Dynamic);
    setbigintsmi(&b, 10, BigIntMode_Dynamic);
    
    luaZ_bigint_add(L, &a, &b, &sum);
    CHECK(ttisbigint(&sum));
    CHECK(sum.value.l == 15);
    
    luaZ_bigint_sub(L, &a, &b, &diff);
    CHECK(ttisbigint(&diff));
    CHECK(diff.value.l == -5);
    
    luaZ_bigint_mul(L, &a, &b, &prod);
    CHECK(ttisbigint(&prod));
    CHECK(prod.value.l == 50);
    
    luaZ_bigint_div(L, &b, &a, &div);
    CHECK(ttisbigint(&div));
    CHECK(div.value.l == 2);
    
    TValue mod_a, mod_b;
    setbigintsmi(&mod_a, 11, BigIntMode_Dynamic);
    setbigintsmi(&mod_b, 4, BigIntMode_Dynamic);
    luaZ_bigint_mod(L, &mod_a, &mod_b, &mod);
    CHECK(ttisbigint(&mod));
    CHECK(mod.value.l == 3);
    
    lua_close(L);
}

TEST_CASE("BigInt_Heap_Multiplication_And_Addition") {
    lua_State* L = luaL_newstate();
    
    // 2^60
    int64_t large_val = 1LL << 60;
    TValue a;
    setbigintsmi(&a, large_val, BigIntMode_Dynamic);
    
    // (2^60) * (2^60) = 2^120
    // This will definitely overflow int64_t and fallback to HeapBigInt
    TValue prod;
    luaZ_bigint_mul(L, &a, &a, &prod);
    
    REQUIRE(ttype(&prod) == LUA_THEAPBIGINT);
    HeapBigInt* heap_prod = (HeapBigInt*)prod.value.gc;
    CHECK(heap_prod->isNegative == false);
    
    // 2^120 in base 2^32 has digits. 
    // 120 / 32 = 3.75, so 4 digits.
    // 2^120 = (2^24) * (2^32)^3
    // Digits: [0, 0, 0, 2^24]
    REQUIRE(heap_prod->size == 4);
    CHECK(heap_prod->digits[0] == 0);
    CHECK(heap_prod->digits[1] == 0);
    CHECK(heap_prod->digits[2] == 0);
    CHECK(heap_prod->digits[3] == (1U << 24));
    
    // Now test addition on heap bigints
    // (2^120) + (2^120) = 2^121
    TValue sum;
    luaZ_bigint_add(L, &prod, &prod, &sum);
    REQUIRE(ttype(&sum) == LUA_THEAPBIGINT);
    HeapBigInt* heap_sum = (HeapBigInt*)sum.value.gc;
    REQUIRE(heap_sum->size == 4);
    CHECK(heap_sum->digits[0] == 0);
    CHECK(heap_sum->digits[1] == 0);
    CHECK(heap_sum->digits[2] == 0);
    CHECK(heap_sum->digits[3] == (1U << 25)); // 2^25 * 2^96 = 2^121
    
    // Test subtraction
    // (2^121) - (2^120) = 2^120
    TValue diff;
    luaZ_bigint_sub(L, &sum, &prod, &diff);
    REQUIRE(ttype(&diff) == LUA_THEAPBIGINT);
    HeapBigInt* heap_diff = (HeapBigInt*)diff.value.gc;
    REQUIRE(heap_diff->size == 4);
    CHECK(heap_diff->digits[3] == (1U << 24));
    
    // Subtraction that yields negative
    // (2^120) - (2^121) = -2^120
    TValue neg_diff;
    luaZ_bigint_sub(L, &prod, &sum, &neg_diff);
    REQUIRE(ttype(&neg_diff) == LUA_THEAPBIGINT);
    HeapBigInt* heap_neg_diff = (HeapBigInt*)neg_diff.value.gc;
    CHECK(heap_neg_diff->isNegative == true);
    REQUIRE(heap_neg_diff->size == 4);
    CHECK(heap_neg_diff->digits[3] == (1U << 24));

    lua_close(L);
}

TEST_CASE("BigInt_Heap_Division_And_Modulo") {
    lua_State* L = luaL_newstate();
    
    int64_t large_val = 1LL << 60;
    TValue a;
    setbigintsmi(&a, large_val, BigIntMode_Dynamic);
    
    TValue num;
    luaZ_bigint_mul(L, &a, &a, &num); // 2^120
    
    TValue div;
    setbigintsmi(&div, 3, BigIntMode_Dynamic);
    
    // 2^120 / 3
    TValue q;
    luaZ_bigint_div(L, &num, &div, &q);
    REQUIRE(ttype(&q) == LUA_THEAPBIGINT);
    HeapBigInt* heap_q = (HeapBigInt*)q.value.gc;
    CHECK(heap_q->isNegative == false);
    
    TValue rem;
    luaZ_bigint_rem(L, &num, &div, &rem);
    
    // 2^120 mod 3
    // 2 = -1 mod 3 -> 2^120 = (-1)^120 mod 3 = 1 mod 3
    CHECK(ttisbigint(&rem)); // Should pack down to SMI!
    CHECK(rem.value.l == 1);
    
    lua_close(L);
}

TEST_SUITE_END();
