#include "doctest.h"
#include "lua.h"
#include "lualib.h"
#include "lobject.h"
#include <vector>
#include <string>

TEST_SUITE_BEGIN("IntegerTests");

TEST_CASE("IntegerSMIArithmetic") {
    lua_State* L = luaL_newstate();
    
    TValue a, b, sum, diff, prod, div, mod;
    setintegersmi(&a, 5, IntegerMode_Dynamic);
    setintegersmi(&b, 10, IntegerMode_Dynamic);
    
    luaZ_integer_add(L, &a, &b, &sum);
    CHECK(ttisinteger(&sum));
    CHECK(sum.value.l == 15);
    
    luaZ_integer_sub(L, &a, &b, &diff);
    CHECK(ttisinteger(&diff));
    CHECK(diff.value.l == -5);
    
    luaZ_integer_mul(L, &a, &b, &prod);
    CHECK(ttisinteger(&prod));
    CHECK(prod.value.l == 50);
    
    luaZ_integer_div(L, &b, &a, &div);
    CHECK(ttisinteger(&div));
    CHECK(div.value.l == 2);
    
    TValue mod_a, mod_b;
    setintegersmi(&mod_a, 11, IntegerMode_Dynamic);
    setintegersmi(&mod_b, 4, IntegerMode_Dynamic);
    luaZ_integer_mod(L, &mod_a, &mod_b, &mod);
    CHECK(ttisinteger(&mod));
    CHECK(mod.value.l == 3);
    
    lua_close(L);
}

TEST_CASE("IntegerHeapArithmetic") {
    lua_State* L = luaL_newstate();
    
    // 2^60
    int64_t large_val = 1LL << 60;
    TValue a;
    setintegersmi(&a, large_val, IntegerMode_Dynamic);
    
    // (2^60) * (2^60) = 2^120
    // This will definitely overflow int64_t and fallback to HeapInteger
    TValue prod;
    luaZ_integer_mul(L, &a, &a, &prod);
    
    REQUIRE(ttype(&prod) == LUA_THEAPINTEGER);
    HeapInteger* heap_prod = (HeapInteger*)prod.value.gc;
    CHECK(heap_prod->isNegative == false);
    
    // 2^120 in base 2^64 has digits. 
    // 120 / 64 = 1.875, so 2 digits.
    // 2^120 = (2^56) * (2^64)^1
    // Digits: [0, 2^56]
    REQUIRE(heap_prod->size == 2);
    CHECK(heap_prod->digits[0] == 0);
    CHECK(heap_prod->digits[1] == (1ULL << 56));
    
    // Now test addition on heap integers
    // (2^120) + (2^120) = 2^121
    TValue sum;
    luaZ_integer_add(L, &prod, &prod, &sum);
    REQUIRE(ttype(&sum) == LUA_THEAPINTEGER);
    HeapInteger* heap_sum = (HeapInteger*)sum.value.gc;
    REQUIRE(heap_sum->size == 2);
    CHECK(heap_sum->digits[0] == 0);
    CHECK(heap_sum->digits[1] == (1ULL << 57)); // 2^121
    
    // Test subtraction
    // (2^121) - (2^120) = 2^120
    TValue diff;
    luaZ_integer_sub(L, &sum, &prod, &diff);
    REQUIRE(ttype(&diff) == LUA_THEAPINTEGER);
    HeapInteger* heap_diff = (HeapInteger*)diff.value.gc;
    REQUIRE(heap_diff->size == 2);
    CHECK(heap_diff->digits[1] == (1ULL << 56));
    
    // Subtraction that yields negative
    // (2^120) - (2^121) = -2^120
    TValue neg_diff;
    luaZ_integer_sub(L, &prod, &sum, &neg_diff);
    REQUIRE(ttype(&neg_diff) == LUA_THEAPINTEGER);
    HeapInteger* heap_neg_diff = (HeapInteger*)neg_diff.value.gc;
    CHECK(heap_neg_diff->isNegative == true);
    REQUIRE(heap_neg_diff->size == 2);
    CHECK(heap_neg_diff->digits[1] == (1ULL << 56));
    setintegersmi(&a, large_val, IntegerMode_Dynamic);
    
    TValue num;
    luaZ_integer_mul(L, &a, &a, &num); // 2^120
    
    TValue div;
    setintegersmi(&div, 3, IntegerMode_Dynamic);
    
    // 2^120 / 3
    TValue q;
    luaZ_integer_div(L, &num, &div, &q);
    REQUIRE(ttype(&q) == LUA_THEAPINTEGER);
    HeapInteger* heap_q = (HeapInteger*)q.value.gc;
    CHECK(heap_q->isNegative == false);
    
    TValue rem;
    luaZ_integer_rem(L, &num, &div, &rem);
    
    // 2^120 mod 3
    // 2 = -1 mod 3 -> 2^120 = (-1)^120 mod 3 = 1 mod 3
    CHECK(ttisinteger(&rem)); // Should pack down to SMI!
    CHECK(rem.value.l == 1);
    
    // Fibonacci tests
    const char* str_fib499 = "86168291600238450732788312165664788095941068326060883324529903470149056115823592713458328176574447204501";
    const char* str_fib500 = "139423224561697880139724382870407283950070256587697307264108962948325571622863290691557658876222521294125";
    const char* str_add = "225591516161936330872512695036072072046011324913758190588638866418474627738686883405015987052796968498626";
    const char* str_sub = "53254932961459429406936070704742495854129188261636423939579059478176515507039697978099330699648074089624";
    const char* str_mul = "12013861069877910697046065629587665527502542343151321692968056436291025827130044418293878005135030754880003465999840382647488541120853879264061494575123040289146444940739753915331989848684667302228051044856625";
    
    TValue t_fib499, t_fib500;
    luaZ_integer_fromstring(L, str_fib499, &t_fib499);
    luaZ_integer_fromstring(L, str_fib500, &t_fib500);
    
    TValue t_add, t_sub, t_mul, t_div, t_mod;
    
    luaZ_integer_add(L, &t_fib499, &t_fib500, &t_add);
    luaZ_pushinteger_string(L, &t_add);
    CHECK(std::string(lua_tostring(L, -1)) == std::string(str_add));
    lua_pop(L, 1);
    
    luaZ_integer_sub(L, &t_fib500, &t_fib499, &t_sub);
    luaZ_pushinteger_string(L, &t_sub);
    CHECK(std::string(lua_tostring(L, -1)) == std::string(str_sub));
    lua_pop(L, 1);
    
    luaZ_integer_mul(L, &t_fib500, &t_fib499, &t_mul);
    luaZ_pushinteger_string(L, &t_mul);
    CHECK(std::string(lua_tostring(L, -1)) == std::string(str_mul));
    lua_pop(L, 1);
    
    luaZ_integer_div(L, &t_fib500, &t_fib499, &t_div);
    luaZ_pushinteger_string(L, &t_div);
    CHECK(std::string(lua_tostring(L, -1)) == "1");
    lua_pop(L, 1);
    
    luaZ_integer_mod(L, &t_fib500, &t_fib499, &t_mod);
    luaZ_pushinteger_string(L, &t_mod);
    CHECK(std::string(lua_tostring(L, -1)) == std::string(str_sub)); // 500 % 499 = 500 - 499
    lua_pop(L, 1);
    
    lua_close(L);
}

TEST_CASE("IntegerFromToString") {
    lua_State* L = luaL_newstate();
    
    auto check_tostring = [&](const char* str) {
        TValue val;
        luaZ_integer_fromstring(L, str, &val);
        luaZ_pushinteger_string(L, &val);
        const char* res = lua_tostring(L, -1);
        CHECK(std::string(res) == std::string(str));
        lua_pop(L, 1);
        
        // Also test negative
        std::string neg_str = "-" + std::string(str);
        if (str[0] == '0' && str[1] == '\0') return; // Don't test -0
        
        TValue neg_val;
        luaZ_integer_fromstring(L, neg_str.c_str(), &neg_val);
        luaZ_pushinteger_string(L, &neg_val);
        const char* neg_res = lua_tostring(L, -1);
        CHECK(std::string(neg_res) == neg_str);
        lua_pop(L, 1);
    };

    // Very small (3-4 digits, fits in SMI)
    check_tostring("0");
    check_tostring("9");
    check_tostring("123");
    check_tostring("9999");
    
    // Small (10-15 digits, fits in SMI or single limb)
    check_tostring("1234567890");
    check_tostring("987654321012345");
    
    // Medium (20-30 digits, spans 2 limbs)
    check_tostring("123456789012345678901234567890");
    check_tostring("999999999999999999999999999999");
    
    // Large (50-100 digits)
    check_tostring("12345678901234567890123456789012345678901234567890");
    check_tostring("9876543210987654321098765432109876543210987654321098765432109876543210987654321098765432109876543210");
    check_tostring("1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    
    // Exact edge case for chunking (10^19 - 1)
    check_tostring("9999999999999999999");
    // Exact edge case for chunking (10^19)
    check_tostring("10000000000000000000");

    // Huge (200-300 digits)
    check_tostring("1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
                   "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890");
    check_tostring("9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999"
                   "9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999"
                   "9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999");
                   
    // 500th Fibonacci number
    check_tostring("139423224561697880139724382870407283950070256587697307264108962948325571622863290691557658876222521294125");

    lua_close(L);
}

TEST_SUITE_END();
