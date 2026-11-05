#pragma once
#include "common.h"

enum { WC_CHEST=1,WC_CONTRACT,WC_TRANSACTION,WC_NOTIFICATION,WC_LABEL,WC_LINK };
enum { WC_MAIN,WC_INVENTORY,WC_STOCK,WC_OFFERED,WC_SETTINGS,WC_DETAILS,WC_NODES,WC_TYPES,WC_FORM,WC_REVIEW,WC_RESULT,WC_TERMS,WC_NETWORKS,WC_NETWORK_DETAIL,WC_CONNECTIONS,WC_TEXT };
enum { WC_BACK=1,WC_REFRESH,WC_DEPOSIT,WC_WITHDRAW,WC_CONFIGURE,WC_UPGRADE,WC_SAVE_SETTINGS,WC_REVIEW_ACTION,WC_EXECUTE,WC_OFFER,WC_REMOVE_OFFER,WC_ACCEPT,WC_SHOW_TERMS,WC_PAYMENTS,WC_REWARDS,WC_CONTRACT_DETAILS,WC_SAVE_CONTRACT,WC_ADD_NODE,WC_DELETE_NODE,WC_NODE_CREDITS,WC_NODE_TYPE,WC_NODE_OPTION,WC_SAVE_NODE,WC_CANCEL,WC_DONE,WC_CLOSE,WC_LINK_CREATE,WC_LINK_CONNECT,WC_LINK_DISCONNECT,WC_LINK_RENAME,WC_LINK_DELETE,WC_LINK_CONNECTIONS,WC_LINK_LOCAL,WC_READ_TEXT };
#define WC_BUTTONS 6
#define WC_PAGE_ROWS 4

typedef struct {int action;bool enabled;char label[64];} WiredChestButton;
typedef struct {
    int kind,view,page,rows,button_count,field_count;
    u32 item,revision;
    bool open,pending;
    char title[128],body[1200],status[192],values[2][4001];
    WiredChestButton buttons[WC_BUTTONS];
} WiredChestPanel;
_Static_assert(sizeof(WiredChestPanel)<=12288,"Chest panel memory budget");
extern WiredChestPanel wired_chest_panel;
void wired_chests_open(u32 item);
void wired_chests_close(void);
void wired_chests_reset(void);
void wired_chests_update(void);
bool wired_chests_receive(const char *text);
void wired_chests_action(int action);
void wired_chests_select(int row);
void wired_chests_page(int step);
bool wired_chests_has_page(int step);
bool wired_chests_row_enabled(int row);
/** -1 when the row has no checkbox, otherwise 0/1 for unselected/selected furniture. */
int wired_chests_row_check(int row);
void wired_chests_row(int row,char *title,size_t title_size,char *detail,size_t detail_size);
bool wired_chests_edit(int field,const char *text);
const char *wired_chests_field_label(int field);
const char *wired_chests_field_value(int field);
int wired_chests_field_limit(int field);
void wired_chests_draw3ds(u64 now);
