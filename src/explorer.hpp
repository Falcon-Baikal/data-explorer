#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtWidgets>
#include <QtGui>
#include <QIcon>
#include <QMdiArea>
#include <string>
#include <vector>
#include "netcdf.h"

class MainWindow;
class ItemData;
class TableWidget;

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

public:
  void set_main_window(MainWindow *p)
  {
    m_main_window = p;
  }

private:
  MainWindow *m_main_window;
  void load_item(QTreeWidgetItem *);
  void* load_variable(const int nc_id, const int var_id, const nc_type var_type, size_t buf_sz);
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow
/////////////////////////////////////////////////////////////////////////////////////////////////////

class MainWindow : public QMainWindow
{
  Q_OBJECT
public:
  MainWindow();
  void add_table(ItemData *item_data);
  int read_file(QString file_name);

  private slots:
  void open_recent_file();
  void open_file();
  void open_dap();
  void about();

private:

  ///////////////////////////////////////////////////////////////////////////////////////
  //widgets
  ///////////////////////////////////////////////////////////////////////////////////////

  QMenu *m_menu_file;
  QMenu *m_menu_help;
  QToolBar *m_tool_bar;
  QMdiArea *m_mdi_area;
  FileTreeWidget *m_tree;
  QDockWidget *m_tree_dock;

  ///////////////////////////////////////////////////////////////////////////////////////
  //actions
  ///////////////////////////////////////////////////////////////////////////////////////

  QAction *m_action_open;
  QAction *m_action_opendap;
  QAction *m_action_exit;
  QAction *m_action_about;

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

private:
  int iterate(const std::string& file_name, const int grp_id, QTreeWidgetItem *tree_item);
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ChildWindow
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ChildWindow : public QMainWindow
{
  Q_OBJECT
public:
  ChildWindow(QWidget *parent, ItemData *item_data);

  private slots:
  void previous_layer(int);
  void next_layer(int);
  void combo_layer(int);

private:

  ///////////////////////////////////////////////////////////////////////////////////////
  //widgets
  ///////////////////////////////////////////////////////////////////////////////////////

  TableWidget *m_table;
  QToolBar *m_tool_bar;
  std::vector<QComboBox *> m_vec_combo;
};

#endif

