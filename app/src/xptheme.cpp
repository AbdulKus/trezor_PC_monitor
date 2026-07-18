#include "xptheme.h"

#include <QFont>
#include <QPalette>
#include <QStyleFactory>

void XpTheme::apply(QApplication &application, const QString &variant) {
  application.setStyle(QStyleFactory::create("Fusion"));
  application.setFont(QFont(QStringLiteral("Segoe UI"), 10));
  const bool light = variant.compare("Light", Qt::CaseInsensitive) == 0;
  const QColor window = light ? QColor("#f1f5f9") : QColor("#0f172a");
  const QColor panel = light ? QColor("#ffffff") : QColor("#182235");
  const QColor input = light ? QColor("#ffffff") : QColor("#0b1220");
  const QColor text = light ? QColor("#172033") : QColor("#e8eef8");
  const QColor muted = light ? QColor("#64748b") : QColor("#94a3b8");
  const QColor border = light ? QColor("#cbd5e1") : QColor("#334155");
  const QColor accent = variant.compare("Forest", Qt::CaseInsensitive) == 0
                            ? QColor("#22c55e")
                            : QColor("#38bdf8");
  QPalette palette;
  palette.setColor(QPalette::Window, window);
  palette.setColor(QPalette::WindowText, text);
  palette.setColor(QPalette::Button, panel);
  palette.setColor(QPalette::ButtonText, text);
  palette.setColor(QPalette::Base, input);
  palette.setColor(QPalette::AlternateBase, panel);
  palette.setColor(QPalette::Text, text);
  palette.setColor(QPalette::PlaceholderText, muted);
  palette.setColor(QPalette::Highlight, accent);
  palette.setColor(QPalette::HighlightedText, QColor("#07111f"));
  palette.setColor(QPalette::ToolTipBase, panel);
  palette.setColor(QPalette::ToolTipText, text);
  application.setPalette(palette);
  application.setStyleSheet(QString(R"(
    * { outline: none; }
    QMainWindow, QDialog { background: %1; }
    QMenuBar { background: %1; padding: 4px; }
    QMenuBar::item { padding: 7px 11px; border-radius: 6px; }
    QMenuBar::item:selected, QMenu::item:selected { background: %5; color: #07111f; }
    QMenu { background: %2; border: 1px solid %4; padding: 6px; }
    QMenu::item { padding: 7px 28px 7px 10px; border-radius: 5px; }
    QTabWidget::pane { border: 1px solid %4; border-radius: 10px; top: -1px; background: %2; }
    QTabBar::tab { min-height: 25px; padding: 8px 18px; margin-right: 4px;
                   color: %6; background: transparent; border-bottom: 3px solid transparent; }
    QTabBar::tab:hover { color: %3; background: %1; }
    QTabBar::tab:selected { color: %3; border-bottom-color: %5; font-weight: 600; }
    QGroupBox { border: 1px solid %4; border-radius: 9px; margin-top: 14px;
                padding: 14px 12px 10px; font-weight: 600; background: %2; }
    QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }
    QListWidget, QTableWidget, QLineEdit, QSpinBox, QComboBox {
      border: 1px solid %4; border-radius: 7px; background: %1; color: %3;
      selection-background-color: %5; selection-color: #07111f; padding: 5px;
    }
    QLineEdit, QSpinBox, QComboBox { min-height: 24px; }
    QHeaderView::section { background: %2; color: %6; padding: 8px;
                           border: none; border-bottom: 1px solid %4; }
    QPushButton { min-height: 30px; padding: 2px 14px; border: 1px solid %4;
                  border-radius: 7px; background: %2; color: %3; font-weight: 500; }
    QPushButton:hover { border-color: %5; background: %1; }
    QPushButton:pressed { background: %5; color: #07111f; }
    QPushButton:disabled { color: %6; }
    QProgressBar { min-height: 18px; border: 1px solid %4; border-radius: 7px;
                   text-align: center; background: %1; }
    QProgressBar::chunk { background: %5; border-radius: 6px; }
    QStatusBar { background: %1; color: %6; border-top: 1px solid %4; }
    QSplitter::handle { background: %4; width: 1px; margin: 8px; }
    QCheckBox { spacing: 8px; }
  )").arg(window.name(), panel.name(), text.name(), border.name(),
           accent.name(), muted.name()));
}
