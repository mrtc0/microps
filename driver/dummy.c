/*
    ダミーデバイスの実装
    - 入力 ... なし(データを受信することはない)
    - 出力 ... データを破棄
*/
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "util.h"
#include "net.h"

#define DUMMY_MTU UINT16_MAX /* IP datagram の最大サイズ 65535 */

static int dummy_transmit(struct net_device *dev, uint16_t type, const uint8_t *data, size_t len, const void *dst)
{
    debugf("dev=%s, type=0x%04x,len=%zu", dev->name, type, len);
    debugdump(data, len);

    return 0;
}

// ダミーデバイスに登録する関数
static struct net_device_ops dummy_ops = {
    .transmit = dummy_transmit, // 送信処理を行う関数
};

struct net_device * dummy_init(void)
{
    struct net_device *dev;

    // デバイスを生成
    dev = net_device_alloc();
    if (!dev) {
        errorf("net_device_alloc() failure");
        return NULL;
    }

    dev->type = NET_DEVICE_TYPE_DUMMY;
    dev->mtu = DUMMY_MTU;
    // ヘッダもアドレスも存在しない
    dev->hlen = 0;
    dev->alen = 0;
    dev->ops = &dummy_ops;

    // プロトコルスタックにデバイスを登録
    if (net_device_register(dev) == -1) {
        errorf("net_device_register() failure");
        return NULL;
    }

    debugf("device(%s) initialized.", dev->name);
    return dev;
}