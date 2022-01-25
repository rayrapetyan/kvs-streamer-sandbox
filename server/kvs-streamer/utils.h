#ifndef __UTILS_INCLUDE__
#define __UTILS_INCLUDE__

#pragma once

template<typename ... Args>
std::string string_format( const std::string& format, Args ... args );

#endif /* __UTILS_INCLUDE__ */
