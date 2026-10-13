#pragma once
#include <windows.h>
// Colisões conhecidas: macros do SDK vs. métodos gerados pelo C++/WinRT.
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#ifdef GetObject
#undef GetObject
#endif
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>

// Code-behind visível em todas as TUs, inclusive .g.cpp gerados pelo XAML
// (eles referenciam implementation::* mas não incluem estes headers).
#include "MainWindow.h"
#include "MainViewModel.h"
#include "App.h"
#include <winrt/Microsoft.UI.Windowing.h>
