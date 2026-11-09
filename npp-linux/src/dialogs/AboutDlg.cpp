#include "AboutDlg.h"
#include "Version.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

AboutDlg::AboutDlg(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("关于 Notepad++"));
    setWindowIcon(QIcon(QStringLiteral(":/app/icons/app_64.png")));
    setModal(true);

#if defined(__x86_64__) || defined(_M_X64)
    const QString bitness = QStringLiteral("(64-bit)");
#elif defined(__aarch64__)
    const QString bitness = QStringLiteral("(ARM 64-bit)");
#else
    const QString bitness = QStringLiteral("(32-bit)");
#endif

    _versionStr = QStringLiteral("v%1 %2").arg(QString::fromUtf8(NPP_VERSION_STRING), bitness);
    _buildTimeStr = QStringLiteral("Build time: %1 - %2").arg(QString::fromLatin1(__DATE__), QString::fromLatin1(__TIME__));

    setupUi();
}

void AboutDlg::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 20);
    mainLayout->setSpacing(16);

    // 头部区域：左侧大图标，右侧标题与版本信息
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(20);

    auto* iconLabel = new QLabel(this);
    QPixmap appPixmap(QStringLiteral(":/app/icons/app_128.png"));
    if (!appPixmap.isNull()) {
        iconLabel->setPixmap(appPixmap.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    iconLabel->setFixedSize(72, 72);
    headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    auto* titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(4);

    auto* titleLabel = new QLabel(QStringLiteral("Notepad++"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLayout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel(QStringLiteral("Linux Native Port (Qt6 & Scintilla)"), this);
    QFont subFont = subtitleLabel->font();
    subFont.setPointSize(subFont.pointSize() - 1);
    subtitleLabel->setFont(subFont);
    titleLayout->addWidget(subtitleLabel);

    auto* versionLabel = new QLabel(_versionStr, this);
    QFont verFont = versionLabel->font();
    verFont.setBold(true);
    versionLabel->setFont(verFont);
    titleLayout->addWidget(versionLabel);

    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch(1);
    mainLayout->addLayout(headerLayout);

    // 分隔线
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(sep);

    // 详情区域
    auto* detailsLayout = new QVBoxLayout();
    detailsLayout->setSpacing(6);

    auto* buildTimeLabel = new QLabel(_buildTimeStr, this);
    detailsLayout->addWidget(buildTimeLabel);

    const QString envInfo = QStringLiteral("Qt %1 | GCC %2 | %3")
                                .arg(QString::fromLatin1(qVersion()),
                                     QString::fromLatin1(__VERSION__),
                                     QSysInfo::prettyProductName());
    auto* envLabel = new QLabel(envInfo, this);
    detailsLayout->addWidget(envLabel);

    auto* homeLabel = new QLabel(
        QStringLiteral("官方网站: <a href=\"https://notepad-plus-plus.org\">https://notepad-plus-plus.org</a>"),
        this);
    homeLabel->setOpenExternalLinks(true);
    detailsLayout->addWidget(homeLabel);

    auto* repoLabel = new QLabel(
        QStringLiteral("移植仓库: <a href=\"https://gitee.com/twypx/notepad-plus-plus-linux\">Gitee: twypx/notepad-plus-plus-linux</a>"),
        this);
    repoLabel->setOpenExternalLinks(true);
    detailsLayout->addWidget(repoLabel);

    auto* licenseLabel = new QLabel(
        QStringLiteral("开源协议: GNU General Public License version 3 (GPL-3.0)"),
        this);
    detailsLayout->addWidget(licenseLabel);

    mainLayout->addLayout(detailsLayout);

    mainLayout->addStretch(1);

    // 底部按钮
    auto* buttonBox = new QDialogButtonBox(this);
    auto* okBtn = buttonBox->addButton(QStringLiteral("确定(&O)"), QDialogButtonBox::AcceptRole);
    okBtn->setDefault(true);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(buttonBox);

    adjustSize();
    setMinimumWidth(440);
}
