#pragma once

#include "layout/groups.hpp"

namespace layoutfix {

enum class UndoRestore { Original, Shown, Drop };

// Старі keycode можна набирати лише в мові, з якої слово виправляли.
// Якщо група лишилась виправленою, назад іде слово, яке вже було на екрані.
inline UndoRestore choose_undo_restore(bool confirmed, Lang now, Lang source, Lang shown) {
    if (confirmed || now == source) {
        return UndoRestore::Original;
    }
    if (now == shown &&
        (shown == Lang::En || shown == Lang::Uk || shown == Lang::Ru)) {
        return UndoRestore::Shown;
    }
    return UndoRestore::Drop;
}

}  // namespace layoutfix
