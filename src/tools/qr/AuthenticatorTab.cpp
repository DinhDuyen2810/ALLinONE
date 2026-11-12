#include "AuthenticatorTab.h"

#include "QRCodec.h"
#include "QRUiStyle.h"
#include "ScreenSnipOverlay.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr int kColIssuer = 0, kColName = 1, kColCode = 2, kColTime = 3;
}

AuthenticatorTab::AuthenticatorTab(QWidget* parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    // ---------- Thanh nhập ----------
    auto* bar = new QHBoxLayout();
    bar->setSpacing(8);
    auto addBtn = [&](const QString& text, auto slot, QLayout* into) {
        auto* b = new QPushButton(text, this);
        b->setStyleSheet(QRUi::buttonStyle());
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, slot);
        into->addWidget(b);
        return b;
    };
    addBtn("📁 Mở ảnh QR...", &AuthenticatorTab::openImage, bar);
    addBtn("📋 Dán ảnh (Ctrl+V)", &AuthenticatorTab::pasteImage, bar);
    addBtn("🖥 Chụp vùng màn hình", &AuthenticatorTab::snipScreen, bar);
    addBtn("⌨ Nhập khóa tay...", &AuthenticatorTab::addManual, bar);
    bar->addStretch();
    root->addLayout(bar);

    auto* hint = new QLabel("Chọn ảnh/mã QR từ Google Authenticator (“Xuất tài khoản” hoặc mã QR khi bật 2FA ở trang web), "
                            "hoặc kéo-thả ảnh vào đây. Mã quét được từ tab “Quét mã QR” cũng thêm được ở đó.",
                            this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #57606a; font-size: 11px;");
    root->addWidget(hint);

    // ---------- Bảng mã ----------
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({"Dịch vụ", "Tài khoản", "Mã", "Còn lại"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColIssuer, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColCode, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColTime, QHeaderView::ResizeToContents);
    m_table->setStyleSheet(QRUi::tableStyle());
    root->addWidget(m_table, 1);

    m_empty = new QLabel("Chưa có tài khoản nào. Thêm bằng ảnh mã QR hoặc nhập khóa tay ở trên.", this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setStyleSheet("color: #8c959f; font-size: 13px; padding: 18px;");
    root->addWidget(m_empty, 1);

    // ---------- Thanh thao tác trên tài khoản đang chọn ----------
    auto* ops = new QHBoxLayout();
    ops->setSpacing(8);
    m_copyBtn = addBtn("📋 Sao chép mã", &AuthenticatorTab::copyCode, ops);
    m_nextBtn = addBtn("➕ Mã HOTP kế tiếp", &AuthenticatorTab::nextHotp, ops);
    m_editBtn = addBtn("✏ Sửa tên", &AuthenticatorTab::editName, ops);
    m_exportBtn = addBtn("🔳 Xuất mã QR", &AuthenticatorTab::exportQr, ops);
    m_removeBtn = addBtn("🗑 Xóa", &AuthenticatorTab::removeAccount, ops);
    ops->addStretch();
    root->addLayout(ops);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);
    setStatus("Khóa bí mật được mã hóa bằng DPAPI của Windows (chỉ tài khoản Windows này đọc được).");

    connect(&AuthenticatorStore::instance(), &AuthenticatorStore::changed, this, &AuthenticatorTab::rebuild);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &AuthenticatorTab::updateButtons);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { copyCode(); });

    auto* paste = new QShortcut(QKeySequence::Paste, this);
    paste->setContext(Qt::WidgetWithChildrenShortcut);
    connect(paste, &QShortcut::activated, this, &AuthenticatorTab::pasteImage);

    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &AuthenticatorTab::tick);
    m_timer->start();

    rebuild();
}

QString AuthenticatorTab::statusText() const
{
    return m_status->text();
}

int AuthenticatorTab::rowCount() const
{
    return m_table->rowCount();
}

void AuthenticatorTab::setStatus(const QString& text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? "color: #cf222e; font-size: 12px;" : "color: #57606a; font-size: 12px;");
}

QString AuthenticatorTab::groupDigits(const QString& code)
{
    // 6 số -> "123 456"; 8 số -> "1234 5678"; khác -> chia đôi
    const int half = (code.size() + 1) / 2;
    return code.size() > 4 ? code.left(half) + " " + code.mid(half) : code;
}

void AuthenticatorTab::rebuild()
{
    const QString keep = [this] {
        bool ok = false;
        const OtpAccount a = currentAccount(&ok);
        return ok ? a.id : QString();
    }();
    const auto& accounts = AuthenticatorStore::instance().accounts();
    m_table->setRowCount(accounts.size());
    int select = -1;
    for (int i = 0; i < accounts.size(); ++i)
    {
        const OtpAccount& a = accounts[i];
        auto* issuer = new QTableWidgetItem(a.issuer.isEmpty() ? "(không rõ)" : a.issuer);
        issuer->setData(Qt::UserRole, a.id);
        auto* name = new QTableWidgetItem(a.name);
        auto* code = new QTableWidgetItem();
        QFont f("Consolas", 16, QFont::Bold);
        code->setFont(f);
        code->setForeground(QColor("#0969da"));
        auto* left = new QTableWidgetItem();
        left->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, kColIssuer, issuer);
        m_table->setItem(i, kColName, name);
        m_table->setItem(i, kColCode, code);
        m_table->setItem(i, kColTime, left);
        m_table->setRowHeight(i, 46);
        if (a.id == keep)
            select = i;
    }
    if (select >= 0)
        m_table->selectRow(select);
    else if (!accounts.isEmpty())
        m_table->selectRow(0);
    m_empty->setVisible(accounts.isEmpty());
    m_table->setVisible(!accounts.isEmpty());
    tick();
    updateButtons();
}

void AuthenticatorTab::tick()
{
    const auto& accounts = AuthenticatorStore::instance().accounts();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (int i = 0; i < accounts.size() && i < m_table->rowCount(); ++i)
    {
        const OtpAccount& a = accounts[i];
        m_table->item(i, kColCode)->setText(groupDigits(Otp::code(a, now)));
        QTableWidgetItem* left = m_table->item(i, kColTime);
        if (a.type == OtpAccount::Type::Hotp)
        {
            left->setText("HOTP #" + QString::number(a.counter));
            left->setForeground(QColor("#57606a"));
        }
        else
        {
            const int r = Otp::remainingSeconds(a, now);
            left->setText(QString::number(r) + " s");
            left->setForeground(r <= 5 ? QColor("#cf222e") : QColor("#1a7f37"));
        }
    }
}

OtpAccount AuthenticatorTab::currentAccount(bool* ok) const
{
    const int row = m_table->currentRow();
    const auto& accounts = AuthenticatorStore::instance().accounts();
    if (row >= 0 && row < accounts.size())
    {
        if (ok)
            *ok = true;
        return accounts[row];
    }
    if (ok)
        *ok = false;
    return {};
}

void AuthenticatorTab::updateButtons()
{
    bool ok = false;
    const OtpAccount a = currentAccount(&ok);
    for (QPushButton* b : {m_copyBtn, m_editBtn, m_exportBtn, m_removeBtn})
        b->setEnabled(ok);
    m_nextBtn->setEnabled(ok && a.type == OtpAccount::Type::Hotp);
}

// ---------------------------------------------------------------- nhập

int AuthenticatorTab::importUri(const QString& text)
{
    auto& store = AuthenticatorStore::instance();
    QList<OtpAccount> list;
    QString err;
    if (text.trimmed().startsWith("otpauth-migration://", Qt::CaseInsensitive))
        list = Otp::parseMigration(text, &err);
    else
    {
        OtpAccount a;
        if (Otp::parseUri(text, &a, &err))
            list << a;
    }
    if (list.isEmpty())
    {
        setStatus(err.isEmpty() ? "Không phải mã Authenticator (otpauth://)." : err, true);
        return 0;
    }
    int added = 0, dup = 0;
    QString failure;
    for (const OtpAccount& a : list)
    {
        if (store.add(a))
            ++added;
        else if (store.lastError().startsWith("Tài khoản này đã có"))
            ++dup;
        else
            failure = store.lastError();
    }
    QString msg = QString("✓ Đã thêm %1 tài khoản").arg(added);
    if (dup)
        msg += QString(", %1 tài khoản đã có sẵn (bỏ qua)").arg(dup);
    if (!err.isEmpty())
        msg += ". " + err;
    if (!failure.isEmpty())
    {
        setStatus("Không lưu được: " + failure, true);
        return added;
    }
    setStatus(msg + ".", false);
    return added;
}

int AuthenticatorTab::importImage(const QImage& image, const QString& sourceName)
{
    if (image.isNull())
    {
        setStatus("Ảnh không hợp lệ.", true);
        return 0;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int undecoded = 0;
    const QList<QRDecoded> found = QRCodec::decode(image, &undecoded);
    QApplication::restoreOverrideCursor();
    if (found.isEmpty())
    {
        setStatus(undecoded > 0 ? QString("Thấy mã QR trong %1 nhưng không đọc được (mờ/bị che/hỏng).").arg(sourceName)
                                : QString("Không tìm thấy mã QR trong %1. Thử cắt sát vùng mã hoặc dùng “Chụp vùng màn hình” (nút bên cạnh).").arg(sourceName),
                  true);
        return 0;
    }
    int added = 0, otpCodes = 0;
    for (const QRDecoded& d : found)
        if (Otp::isOtpUri(d.text))
        {
            ++otpCodes;
            added += importUri(d.text);
        }
    if (otpCodes == 0)
    {
        setStatus(QString("Có %1 mã QR trong %2 nhưng không phải mã Authenticator (otpauth://) - đã chuyển sang tab Quét để xem nội dung.").arg(found.size()).arg(sourceName), true);
        emit nonOtpScanned(image, found, sourceName);
    }
    return added;
}

void AuthenticatorTab::openImage()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Chọn ảnh chứa mã QR Authenticator", QString(),
                                                            "Ảnh (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;Tất cả (*.*)");
    for (const QString& path : paths)
    {
        const QImage img(path);
        if (img.isNull())
            setStatus("Không đọc được file ảnh: " + path, true);
        else
            importImage(img, QFileInfo(path).fileName());
    }
}

void AuthenticatorTab::pasteImage()
{
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    QImage img;
    QString name = "clipboard";
    if (md && md->hasImage())
        img = qvariant_cast<QImage>(md->imageData());
    else if (md && md->hasUrls())
        for (const QUrl& u : md->urls())
            if (u.isLocalFile() && !(img = QImage(u.toLocalFile())).isNull())
            {
                name = u.fileName();
                break;
            }
    if (img.isNull() && md && md->hasText() && Otp::isOtpUri(md->text()))
    {
        importUri(md->text()); // người dùng sao chép liên kết otpauth:// dạng chữ
        return;
    }
    if (img.isNull())
    {
        setStatus("Clipboard không có ảnh. Sao chép ảnh mã QR (hoặc chụp màn hình bằng Win+Shift+S) rồi thử lại.", true);
        return;
    }
    importImage(img, name);
}

void AuthenticatorTab::snipScreen()
{
    QWidget* top = window();
    const Qt::WindowStates previousState = top->windowState() & ~Qt::WindowMinimized;
    top->hide();
    QTimer::singleShot(250, this, [this, top, previousState] {
        auto restoreWindow = [top, previousState] {
            top->setWindowState(previousState);
            top->show();
            top->raise();
            top->activateWindow();
        };
        auto* overlay = new ScreenSnipOverlay();
        connect(overlay, &ScreenSnipOverlay::captured, this, [this, restoreWindow](const QImage& img) {
            restoreWindow();
            importImage(img, "vùng màn hình");
        });
        connect(overlay, &ScreenSnipOverlay::cancelled, this, [this, restoreWindow] {
            restoreWindow();
            setStatus("Đã hủy chụp màn hình.");
        });
        overlay->show();
        overlay->raise();
        overlay->activateWindow();
        overlay->setFocus();
    });
}

void AuthenticatorTab::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasImage() || event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void AuthenticatorTab::dropEvent(QDropEvent* event)
{
    const QMimeData* md = event->mimeData();
    if (md->hasUrls())
    {
        for (const QUrl& u : md->urls())
            if (u.isLocalFile())
                importImage(QImage(u.toLocalFile()), u.fileName());
    }
    else if (md->hasImage())
        importImage(qvariant_cast<QImage>(md->imageData()), "ảnh kéo-thả");
    event->acceptProposedAction();
}

void AuthenticatorTab::addManual()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Nhập khóa Authenticator");
    auto* form = new QFormLayout(&dlg);
    auto* issuer = new QLineEdit(&dlg);
    auto* name = new QLineEdit(&dlg);
    auto* secret = new QLineEdit(&dlg);
    secret->setPlaceholderText("Khóa base32 (vd JBSWY3DPEHPK3PXP)");
    auto* type = new QComboBox(&dlg);
    type->addItems({"TOTP (theo thời gian)", "HOTP (theo bộ đếm)"});
    auto* algo = new QComboBox(&dlg);
    algo->addItems({"SHA1", "SHA256", "SHA512"});
    auto* digits = new QSpinBox(&dlg);
    digits->setRange(4, 10);
    digits->setValue(6);
    auto* period = new QSpinBox(&dlg);
    period->setRange(1, 3600);
    period->setValue(30);
    period->setSuffix(" giây");
    for (QWidget* w : {static_cast<QWidget*>(issuer), static_cast<QWidget*>(name), static_cast<QWidget*>(secret), static_cast<QWidget*>(type),
                       static_cast<QWidget*>(algo), static_cast<QWidget*>(digits), static_cast<QWidget*>(period)})
        w->setStyleSheet(QRUi::inputStyle());
    form->addRow("Dịch vụ:", issuer);
    form->addRow("Tài khoản:", name);
    form->addRow("Khóa bí mật:", secret);
    form->addRow("Loại:", type);
    form->addRow("Thuật toán:", algo);
    form->addRow("Số chữ số:", digits);
    form->addRow("Chu kỳ (TOTP):", period);
    auto* ok = new QPushButton("Thêm", &dlg);
    ok->setStyleSheet(QRUi::primaryButtonStyle());
    auto* cancel = new QPushButton("Hủy", &dlg);
    cancel->setStyleSheet(QRUi::buttonStyle());
    auto* row = new QHBoxLayout();
    row->addStretch();
    row->addWidget(cancel);
    row->addWidget(ok);
    form->addRow(row);
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        bool good = false;
        Otp::base32Decode(secret->text(), &good);
        if (!good)
        {
            QMessageBox::warning(&dlg, "Khóa không hợp lệ", "Khóa phải là chuỗi base32 (chữ A-Z và số 2-7).");
            return;
        }
        dlg.accept();
    });
    if (dlg.exec() != QDialog::Accepted)
        return;

    OtpAccount a;
    a.issuer = issuer->text().trimmed();
    a.name = name->text().trimmed();
    a.secret = Otp::base32Decode(secret->text());
    a.type = type->currentIndex() == 1 ? OtpAccount::Type::Hotp : OtpAccount::Type::Totp;
    a.algorithm = algo->currentText();
    a.digits = digits->value();
    a.period = period->value();
    if (a.issuer.isEmpty() && a.name.isEmpty())
        a.name = "Tài khoản mới";
    auto& store = AuthenticatorStore::instance();
    if (store.add(a))
        setStatus("✓ Đã thêm tài khoản.");
    else
        setStatus(store.lastError(), true);
}

// ---------------------------------------------------------------- thao tác

void AuthenticatorTab::copyCode()
{
    bool ok = false;
    const OtpAccount a = currentAccount(&ok);
    if (!ok)
        return;
    const QString code = Otp::code(a, QDateTime::currentSecsSinceEpoch());
    QGuiApplication::clipboard()->setText(code);
    setStatus("✓ Đã sao chép mã " + groupDigits(code) + " của " + a.displayName() + ".");
    if (a.type == OtpAccount::Type::Hotp)
        AuthenticatorStore::instance().advanceCounter(a.id); // mã HOTP đã dùng: sang mã kế
}

void AuthenticatorTab::nextHotp()
{
    bool ok = false;
    const OtpAccount a = currentAccount(&ok);
    if (ok && a.type == OtpAccount::Type::Hotp && AuthenticatorStore::instance().advanceCounter(a.id))
        setStatus("Đã chuyển sang mã HOTP kế tiếp.");
}

void AuthenticatorTab::editName()
{
    bool ok = false;
    OtpAccount a = currentAccount(&ok);
    if (!ok)
        return;
    bool a1 = false;
    const QString issuer = QInputDialog::getText(this, "Sửa tên", "Dịch vụ:", QLineEdit::Normal, a.issuer, &a1);
    if (!a1)
        return;
    bool a2 = false;
    const QString name = QInputDialog::getText(this, "Sửa tên", "Tài khoản:", QLineEdit::Normal, a.name, &a2);
    if (!a2)
        return;
    a.issuer = issuer.trimmed();
    a.name = name.trimmed();
    if (!AuthenticatorStore::instance().update(a))
        setStatus(AuthenticatorStore::instance().lastError(), true);
}

void AuthenticatorTab::exportQr()
{
    bool ok = false;
    const OtpAccount a = currentAccount(&ok);
    if (!ok)
        return;
    const QString uri = Otp::buildUri(a);
    QString err;
    const QRMatrix m = QRCodec::encode(uri, QREcc::Quartile, &err);
    if (!m.isValid())
    {
        setStatus("Không tạo được mã QR: " + err, true);
        return;
    }
    QRStyle style;
    style.targetSize = 340;
    const QImage img = QRCodec::render(m, style);

    QDialog dlg(this);
    dlg.setWindowTitle("Xuất mã QR - " + a.displayName());
    auto* lay = new QVBoxLayout(&dlg);
    auto* pic = new QLabel(&dlg);
    pic->setPixmap(QPixmap::fromImage(img));
    pic->setAlignment(Qt::AlignCenter);
    lay->addWidget(pic);
    auto* warn = new QLabel("⚠ Mã này CHỨA KHÓA BÍ MẬT: ai quét được là tạo được mã đăng nhập của bạn. "
                            "Chỉ quét bằng điện thoại của chính bạn rồi đóng cửa sổ; không chụp/gửi cho người khác.", &dlg);
    warn->setWordWrap(true);
    warn->setStyleSheet("color: #cf222e; font-size: 11px;");
    lay->addWidget(warn);
    auto* save = new QPushButton("💾 Lưu ảnh PNG...", &dlg);
    save->setStyleSheet(QRUi::buttonStyle());
    auto* close = new QPushButton("Đóng", &dlg);
    close->setStyleSheet(QRUi::primaryButtonStyle());
    auto* row = new QHBoxLayout();
    row->addStretch();
    row->addWidget(save);
    row->addWidget(close);
    lay->addLayout(row);
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(save, &QPushButton::clicked, &dlg, [&] {
        const QString path = QFileDialog::getSaveFileName(&dlg, "Lưu mã QR", "authenticator.png", "PNG (*.png)");
        if (!path.isEmpty() && !img.save(path))
            QMessageBox::warning(&dlg, "Lưu ảnh", "Không ghi được tệp.");
    });
    dlg.exec();
}

void AuthenticatorTab::removeAccount()
{
    bool ok = false;
    const OtpAccount a = currentAccount(&ok);
    if (!ok)
        return;
    const auto r = QMessageBox::warning(this, "Xóa tài khoản",
                                        "Xóa “" + a.displayName() + "” khỏi Authenticator?\n\nSau khi xóa bạn KHÔNG còn mã đăng nhập của tài khoản này ở đây. "
                                        "Hãy chắc chắn đã tắt 2FA ở dịch vụ đó hoặc còn mã dự phòng/thiết bị khác.",
                                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes)
        return;
    if (AuthenticatorStore::instance().remove(a.id))
        setStatus("Đã xóa " + a.displayName() + ".");
    else
        setStatus(AuthenticatorStore::instance().lastError(), true);
}
