#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "platform.h"
#include "util.h"
#include "net.h"

static struct net_device *devices; // デバイスのリスト。リストの先頭を指すポインタ

// デバイス構造体のサイズのメモリを確保して、その領域を返す
// デバイスが自身を登録する際に呼び出される
struct net_device * net_device_alloc(void)
{
    struct net_device *dev;

    // memory_alloc() で確保したメモリ領域は0で初期化されている
    dev = memory_alloc(sizeof(*dev));
    if (!dev) {
        errorf("memory_alloc() failure");
        return NULL;
    }

    return dev;
}

// デバイスを登録する
// デバイスが自身を登録する際に呼び出される
int net_device_register(struct net_device *dev)
{
    static unsigned int index = 0;

    // デバイスのインデックス番号を設定する
    dev->index = index++;
    // デバイス名を生成する (net0, net1, net2... となる)
    snprintf(dev->name, sizeof(dev->name), "net%d", dev->index);
    // デバイスリストの先頭に追加する
    dev->next = devices;
    devices = dev;

    infof("device(%s (type(0x%04x)) is registerd.", dev->name, dev->type);
    return 0;
}

int net_device_open(struct net_device *dev)
{
    if (NET_DEVICE_IS_UP(dev)) {
        errorf("device(%s) is already opend.", dev->name);
        return -1;
    }

    // デバイスドライバの open 関数を呼び出す
    if (dev->ops->open) {
        if (dev->ops->open(dev) == -1 ) {
            errorf("device(%s) open failed.", dev->name);
            return -1;
        }
    }

    dev->flags |= NET_DEVICE_FLAG_UP;
    infof("device(%s) upped. state is %s.", dev->name, NET_DEVICE_STATE(dev));

    return 0;
}
int net_device_close(struct net_device *dev)
{
    if(!NET_DEVICE_IS_UP(dev)) {
        errorf("device(%s) is already closed.", dev->name);
        return -1;
    }

    if (dev->ops->close) {
        if (dev->ops->close(dev) == -1) {
            errorf("device(%s) close failed.", dev->name);
            return -1;
        }
    }

    dev->flags &= ~NET_DEVICE_FLAG_UP;
    infof("device(%s) closed. state is %s.", dev->name, NET_DEVICE_STATE(dev));
    return 0;
}

// プロトコルスタックからパケットをデバイスに渡す関数
int net_device_output(struct net_device *dev, uint16_t type, const uint8_t *data, size_t len, const void *dst)
{
    if (!NET_DEVICE_IS_UP(dev)) {
        errorf("device(%s) is not opend.", dev->name);
        return -1;
    }

    if (len > dev->mtu) {
        errorf("mtu too long... dev=%s, mtu=%u, len=%zu", dev->name, dev->mtu, len);
        return -1;
    }

    debugf("dev=%s, type=0x%04x, len=%zu", dev->name, dev->mtu, len);
    debugdump(data, len);

    if (dev->ops->transmit(dev, type, data, len, dst) == -1) {
        errorf("device(%s) transmit failure. length=%zu", dev->name, len);
        return -1;
    }

    return 0;
}

// デバイスが受信したパケットをプロトコルスタックに渡す関数
// デバイスドライバから呼び出される
int net_input_handler(uint16_t type, const uint8_t *data, size_t len, struct net_device *dev) {
    debugf("input from device(%s). type=0x%04x, len=%zu.", dev->name, type, len);
    debugdump(data, len);
    return 0;
}

int net_run(void)
{
    struct net_device *dev;

    if (intr_run() == -1) {
        errorf("Interrupt start failed.");
        return -1;
    }

    debugf("open all devices...");
    for (dev = devices; dev; dev = dev->next) {
        net_device_open(dev);
    }

    return 0;
}

void net_shutdown(void)
{
    intr_shutdown();
    debugf("Procotol stack down.");
}

int net_init(void)
{
    if (intr_init() == -1) {
        errorf("Interrupt initialize failed.");
        return -1;
    }

    infof("protocol stack initialized");
    return 0;
}