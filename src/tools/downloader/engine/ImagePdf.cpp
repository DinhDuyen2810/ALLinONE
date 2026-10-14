#include "ImagePdf.h"

#include <QList>
#include <QString>

namespace
{
QByteArray pt(int px)
{
    return QByteArray::number(px * 0.75, 'f', 2); // 'f' + QByteArray::number luôn dùng dấu chấm thập phân
}
} // namespace

namespace ImagePdf
{
QByteArray build(const std::map<int, Page>& input)
{
    // Lọc trang hợp lệ trước để số đối tượng/xref khớp chính xác.
    QList<const Page*> pages;
    for (const auto& [number, page] : input)
    {
        Q_UNUSED(number)
        if (!page.jpeg.isEmpty() && page.width > 0 && page.height > 0)
            pages << &page;
    }
    if (pages.isEmpty())
        return {};

    const int n = pages.size();
    QByteArray out;
    QList<qint64> offsets(3 * n + 3, 0); // offsets[id] = vị trí byte của "id 0 obj"

    auto begin = [&](int id) {
        offsets[id] = out.size();
        out += QByteArray::number(id) + " 0 obj\n";
    };
    auto simpleObject = [&](int id, const QByteArray& body) {
        begin(id);
        out += body + "\nendobj\n";
    };

    out += "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n"; // dòng chú thích nhị phân: báo cho công cụ khác đây là tệp nhị phân

    // Đánh số: 1 = Catalog, 2 = Pages; trang thứ i (từ 0): Page = 3+3i, Contents = 4+3i, Image = 5+3i.
    QByteArray kids;
    for (int i = 0; i < n; ++i)
        kids += QByteArray::number(3 + 3 * i) + " 0 R ";
    simpleObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    simpleObject(2, "<< /Type /Pages /Count " + QByteArray::number(n) + " /Kids [" + kids + "] >>");

    for (int i = 0; i < n; ++i)
    {
        const Page& pg = *pages[i];
        const int pageId = 3 + 3 * i, contentId = pageId + 1, imageId = pageId + 2;
        const QByteArray w = pt(pg.width), h = pt(pg.height);
        const QByteArray content = "q " + w + " 0 0 " + h + " 0 0 cm /Im0 Do Q";

        simpleObject(pageId, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + w + " " + h + "] /Resources << /XObject << /Im0 " +
                                 QByteArray::number(imageId) + " 0 R >> >> /Contents " + QByteArray::number(contentId) + " 0 R >>");
        simpleObject(contentId, "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream");

        begin(imageId);
        out += "<< /Type /XObject /Subtype /Image /Width " + QByteArray::number(pg.width) + " /Height " +
               QByteArray::number(pg.height) + " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " +
               QByteArray::number(pg.jpeg.size()) + " >>\nstream\n";
        out += pg.jpeg;
        out += "\nendstream\nendobj\n";
    }

    const int size = 3 * n + 3;
    const qint64 xrefPos = out.size();
    out += "xref\n0 " + QByteArray::number(size) + "\n0000000000 65535 f \n";
    for (int id = 1; id < size; ++id) // mỗi dòng ĐÚNG 20 byte: 10 chữ số + " 00000 n " + "\n"
        out += QByteArray::number(offsets[id]).rightJustified(10, '0') + " 00000 n \n";
    out += "trailer\n<< /Size " + QByteArray::number(size) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xrefPos) +
           "\n%%EOF";
    return out;
}
} // namespace ImagePdf
