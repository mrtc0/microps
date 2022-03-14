#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include "util.h"
#include "net.h"
#include "driver/loopback.h"

#include "test.h"

static volatile sig_atomic_t terminate;

static void on_signal(int s)
{
    // シグナルハンドラでは sig_atomic_t 型変数への書き込み以外行わない
    // https://www.jpcert.or.jp/sc-rules/c-sig30-c.html
    (void)s;
    terminate = 1;
}

int main(int argc, char *argv[])
{
    struct net_device *dev;

    signal(SIGINT, on_signal);
    // プロトコルスタックの初期化
    if (net_init() == -1) {
        errorf("net_init() failure");
        return -1;
    }

    dev = loopback_init();
    if (!dev) {
        errorf("loopback_init() failure");
        return -1;
    }
    // プロトコルスタックの起動
    if (net_run() == -1) {
        errorf("net_run() failure");
        return -1;
    }

    // 1秒ごとにデバイスにパケットを書き込む
    // Ctrl+C を押すと terminate に 1 が設定される
    while(!terminate) {
        if (net_device_output(dev, 0x0800, test_data, sizeof(test_data), NULL) == -1) {
            errorf("net_device_output() failure");
            break;
        }
        sleep(1);
    }

    // プロトコルスタックの停止
    net_shutdown();
    return 0;
}