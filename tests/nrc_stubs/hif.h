#pragma once
#include "nrc_sdk.h"
typedef void(*_hif_rxcb_t)(char*,int);enum _HIF_TYPE{_HIF_TYPE_NONE=-1,_HIF_TYPE_HSPI=0};typedef struct{char*addr;int size;}_hif_buf_t;typedef struct{_hif_buf_t buf;_hif_rxcb_t cb;}_hif_rx_params_t;typedef struct{enum _HIF_TYPE type;struct{uint32_t sw_id,bd_id;}hspi;_hif_rx_params_t rx_params;}_hif_info_t;int _hif_open(_hif_info_t*);int _hif_write(char*,int);
