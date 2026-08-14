#include "traypopup.h"

#include <DGuiApplicationHelper>
#include <DListView>
#include <DProgressBar>
#include <DSpinner>
#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

namespace DtkUpdate
{

    TrayPopup::TrayPopup(UpdateMonitor* monitor, QWidget* parent)
        : QWidget(parent), m_monitor(monitor)
    {
        setFixedWidth(280);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 12, 12, 12);
        root->setSpacing(8);

        m_summary = new QLabel(tr("Checking for updates…"));
        m_summary->setWordWrap(true);
        root->addWidget(m_summary);

        m_sevLabel = new QLabel;
        m_sevLabel->setVisible(false);
        root->addWidget(m_sevLabel);

        m_list = new DListView;
        m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->setFixedHeight(160);
        auto* model = new QStandardItemModel(m_list);
        m_list->setModel(model);
        root->addWidget(m_list);

        m_updateBtn = new QPushButton(tr("Update Now"));
        connect(m_updateBtn, &QPushButton::clicked, this, &TrayPopup::onUpdateClicked);
        root->addWidget(m_updateBtn);

        refresh();
        if (m_monitor)
        {
            connect(m_monitor, &UpdateMonitor::updatesAvailable, this, &TrayPopup::refresh);
            connect(m_monitor, &UpdateMonitor::stateChanged, this, &TrayPopup::refresh);
            connect(m_monitor, &UpdateMonitor::securityPrompt, this,
                    [this](const QString& sev, const QList<SecurityAdvisor::Advisory>&)
                    {
                        Q_UNUSED(sev)
                        refresh();
                    });
        }
    }

    void TrayPopup::refresh()
    {
        if (!m_monitor)
            return;
        const auto& pkgs = m_monitor->upgradable();
        const int n = pkgs.size();

        switch (m_monitor->state())
        {
        case UpdateMonitor::State::Checking:
            m_summary->setText(tr("Checking for updates…"));
            m_updateBtn->setEnabled(false);
            break;
        case UpdateMonitor::State::Updating:
            m_summary->setText(tr("Updating…"));
            m_updateBtn->setEnabled(false);
            break;
        case UpdateMonitor::State::Error:
            m_summary->setText(tr("Last check failed. Click to retry."));
            m_updateBtn->setEnabled(false);
            break;
        case UpdateMonitor::State::HasUpdates:
            m_summary->setText(tr("%1 updates available").arg(n));
            m_updateBtn->setEnabled(true);
            break;
        case UpdateMonitor::State::Idle:
        default:
            m_summary->setText(tr("System up to date"));
            m_updateBtn->setEnabled(false);
            break;
        }

        auto* model = qobject_cast<QStandardItemModel*>(m_list->model());
        if (model)
        {
            model->clear();
            const int show = qMin(n, 50);
            for (int i = 0; i < show; ++i)
            {
                const auto& p = pkgs.at(i);
                auto* item =
                    new QStandardItem(QStringLiteral("%1  %2 → %3")
                                          .arg(p.name, p.currentVersion, p.candidateVersion));
                model->appendRow(item);
            }
            if (n > show)
                model->appendRow(new QStandardItem(tr("…and %1 more").arg(n - show)));
        }
    }

    void TrayPopup::onUpdateClicked()
    {
        if (m_monitor)
            m_monitor->applyUpdates();
    }

} // namespace DtkUpdate
