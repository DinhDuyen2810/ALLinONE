#pragma once

// Chụp lại TOÀN BỘ nội dung Clipboard của người dùng (mọi định dạng dạng bộ nhớ HGLOBAL: chữ, ảnh CF_DIB/PNG,
// danh sách tệp, định dạng riêng của ứng dụng...) rồi khôi phục nguyên vẹn khi hủy - dùng cho test buộc phải
// đặt Clipboard thật (CommandLauncher: PSReadLine đọc lệnh từ Clipboard của Windows). Bỏ qua các định dạng là
// handle GDI (CF_BITMAP, metafile, palette): Windows tự tổng hợp lại chúng từ CF_DIB/CF_DIBV5 đã khôi phục.
//
// Đã đo thật: Clipboard đang chứa ảnh chụp màn hình (Preferred DropEffect, System.Drawing.Bitmap, Bitmap, PNG)
// -> chụp, đặt chữ, khôi phục -> đủ lại các định dạng và đúng kích thước ảnh.

#include <cstring>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class ClipboardSnapshot
{
public:
    ClipboardSnapshot() { save(); }
    ~ClipboardSnapshot() { restore(); }
    ClipboardSnapshot(const ClipboardSnapshot&) = delete;
    ClipboardSnapshot& operator=(const ClipboardSnapshot&) = delete;

    bool saved() const { return m_saved; }
    size_t formatCount() const { return m_items.size(); }

    /// Khôi phục ngay (gọi lại nhiều lần được; destructor gọi lần cuối).
    bool restore()
    {
        if (!m_saved || !open())
            return false;
        EmptyClipboard();
        for (const Item& item : m_items)
        {
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, item.data.empty() ? 1 : item.data.size());
            if (!mem)
                continue;
            if (!item.data.empty())
            {
                std::memcpy(GlobalLock(mem), item.data.data(), item.data.size());
                GlobalUnlock(mem);
            }
            if (!SetClipboardData(item.format, mem))
                GlobalFree(mem);
        }
        CloseClipboard();
        return true;
    }

private:
    struct Item
    {
        UINT format;
        std::vector<char> data;
    };

    static bool open()
    {
        for (int attempt = 0; attempt < 100; ++attempt) // ứng dụng khác có thể đang giữ Clipboard vài chục ms
        {
            if (OpenClipboard(nullptr))
                return true;
            Sleep(20);
        }
        return false;
    }

    static bool isHandleFormat(UINT format)
    {
        return format == CF_BITMAP || format == CF_ENHMETAFILE || format == CF_METAFILEPICT || format == CF_PALETTE ||
               format == CF_DSPBITMAP || format == CF_DSPENHMETAFILE || format == CF_DSPMETAFILEPICT ||
               format == CF_OWNERDISPLAY;
    }

    void save()
    {
        if (!open())
            return;
        m_saved = true;
        for (UINT format = EnumClipboardFormats(0); format != 0; format = EnumClipboardFormats(format))
        {
            if (isHandleFormat(format))
                continue;
            HANDLE handle = GetClipboardData(format);
            if (!handle)
                continue;
            const SIZE_T size = GlobalSize(handle);
            const void* data = GlobalLock(handle);
            if (!data)
                continue;
            m_items.push_back({format, std::vector<char>(static_cast<const char*>(data), static_cast<const char*>(data) + size)});
            GlobalUnlock(handle);
        }
        CloseClipboard();
    }

    bool m_saved{false};
    std::vector<Item> m_items;
};
