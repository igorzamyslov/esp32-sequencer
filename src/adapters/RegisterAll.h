#pragma once
namespace seqb {
// Calls every adapter's register*() function in order. Must be called once
// from setup() before sequences/triggers are loaded so the registry is populated.
void registerAllAdapters();
}
