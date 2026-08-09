# `lua_findunuseduserdatatag`, `lua_findunusedlightuserdatatag`

Status: Implemented

## Summary
Implement functions for finding unused userdata and light userdata tags.

## Motivation
It is currently not possible for embedders to know what tags are available for use through just the existing public C API.
Embedders may sometimes want to query for an available tag without having to keep track of every tag by themselves.

## Design

Two new members will be added to the C API: `lua_findunuseduserdatatag`, and `lua_findunusedlightuserdatatag`.

### `int lua_findunuseduserdatatag(lua_State* L)`

This function will find and return a userdata tag that has not been used for any other purpose (userdata metatables, userdata destructors, or userdata direct field access).
If it cannot find any available tags (tags are restricted to the range `[0..LUA_UTAG_LIMIT]`),
then it will return `-1`.

### `int lua_findunusedlightuserdatatag(lua_State* L)`

This function will find and return a light userdata tag that has not been associated with a name.
If it cannot find any available tags (tags are restricted to the range `[0..LUA_LUTAG_LIMIT]`),
then it will return `-1`.
