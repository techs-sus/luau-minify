#pragma once

// HEAVILY inspired by LUAU_RTTI(...)

extern int globalGraphRttiIndex;

template <typename T> struct GraphRtti {
  static const int value;
};

template <typename T> const int GraphRtti<T>::value = ++globalGraphRttiIndex;

#define RTTI(Class)                                                            \
  static int ClassIndex() { return GraphRtti<Class>::value; }
