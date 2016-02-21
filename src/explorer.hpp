#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtWidgets>
#include <QtGui>
#include <QIcon>
#include <QMdiArea>
#include <string>
#include "netcdf.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget
/////////////////////////////////////////////////////////////////////////////////////////////////////

class FileTreeWidget : public QTreeWidget
{
  Q_OBJECT
public:
  FileTreeWidget(QWidget *parent = 0);
  ~FileTreeWidget();
  private slots:
  void show_context_menu(const QPoint &);
  void grid();

private:
  void load_item(QTreeWidgetItem *);
  void* load_variable(const int nc_id, const int var_id, const nc_type var_type, size_t buf_sz);
};

class MainWindow : public QMainWindow
{
  Q_OBJECT
public:
  MainWindow();

private:

  ///////////////////////////////////////////////////////////////////////////////////////
  //widgets
  ///////////////////////////////////////////////////////////////////////////////////////

  QMenu *m_menu_file;
  QToolBar *m_tool_bar;
  QMdiArea *m_mdi_area;
  FileTreeWidget *m_tree;
  QDockWidget *m_tree_dock;

  ///////////////////////////////////////////////////////////////////////////////////////
  //actions
  ///////////////////////////////////////////////////////////////////////////////////////

  QAction *m_action_open;
  QAction *m_action_exit;

  ///////////////////////////////////////////////////////////////////////////////////////
  //icons
  ///////////////////////////////////////////////////////////////////////////////////////

  QIcon m_icon_main;
  QIcon m_icon_group;
  QIcon m_icon_dataset;

  ///////////////////////////////////////////////////////////////////////////////////////
  //recent files
  ///////////////////////////////////////////////////////////////////////////////////////

  enum { max_recent_files = 5 };
  QAction *m_action_recent_file[max_recent_files];
  QAction *m_action_separator_recent;
  QStringList m_sl_recent_files;
  QString m_str_current_file;
  QString stripped_name(const QString &full_file_name);
  void update_recent_file_actions();
  void set_current_file(const QString &file_name);
  void closeEvent(QCloseEvent *eve);

  private slots:
  void open_recent_file();
  void open();

private:
  int read_file(QString file_name);
  int iterate(const std::string& file_name, const int grp_id, QTreeWidgetItem *tree_item);
};

#endif

