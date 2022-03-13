/*
 * 割り込み処理をまとめている。
 *
 * 割り込みによって実行される処理 = 割り込みハンドラ
 * 割り込み要求の発生源を識別するために「割り込み番号」(IRQ番号)を割り当てて
 * IRQ 番号ごとにドライバの入力関数などを割り込みハンドラとして設定する
 *
 * 1. NIC にパケットが到着する
 * 2. CPU に割り込み信号を送る
 * 3. IRQ 番号と割り込みハンドラの対応表から割り込みハンドラのアドレスを取得して呼び出し
 * 4. ドライバがなんか処理してプロトコルスタックへ
 *
 * 上記がカーネルでの流れだが、このプロトコルスタックはユーザーランドで動くため、ハードウェア割り込みを扱うことはできない
 * そのため OS が提供しているシグナルを利用して似た動きを模倣する
 */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "platform.h"
#include "util.h"

// 割り込み要求(IRQ)の構造体
struct irq_entry {
    struct irq_entry *next; // 次の IRQ 構造体へのポインタ
    unsigned int irq; // 割り込み番号(IRQ番号)
    int (*handler)(unsigned int irq, void *dev); // 割り込みハンドラ(割り込み発生時に呼び出す関数へのポインタ)
    int flags;
    char name[16]; // デバッグで使用するために名前を保持する
    void *dev; // 割り込み発生元のデバイス
};

static struct irq_entry *irqs; // IRQ リスト
static sigset_t sigmask; // シグナルの集合
static pthread_t tid; // 割り込み処理スレッドのスレッドID
static pthread_barrier_t barrier;

int intr_request_irq(unsigned int irq, int (*handler)(unsigned int irq, void *dev), int flags, const char *name, void *dev)
{
    struct irq_entry *entry;

    // IRQ 番号がすでに登録されている場合は IRQ 番号が共有されているか確認する
    // どちらかが共有を許可していない場合はエラーを返す
    for (entry = irqs; entry; entry = entry->next) {
        if (entry->irq == irq) {
            if (entry->flags ^ INTR_IRQ_SHARED || flags ^ INTR_IRQ_SHARED) {
                errorf("conflicts with already registerd IRQs");
                return -1;
            }
        }
    }

    // IRQ リストにエントリを追加する
    entry = memory_alloc(sizeof(*entry));
    if (!entry) {
        errorf("memory_alloc() failed.");
        return -1;
    }

    entry->irq = irq;
    entry->handler = handler;
    entry->flags = flags;
    strncpy(entry->name, name, sizeof(entry->name)-1);
    entry->dev = dev;
    entry->next = irqs;
    irqs = entry;
    // シグナルの集合に新しいシグナルを追加する
    sigaddset(&sigmask, irq);
    debugf("IRQ registerd: irq=%u, name=%s", irq, name);

    return 0;
}

// 割り込みを発生させる関数
// これが呼び出されると割り込み処理スレッド(intr_thread)へシグナルが送信される
int intr_raise_irq(unsigned int irq)
{
    debugf("intr_raise_irq() called. signal number: %u", irq);
    // 割り込み処理スレッドへシグナルを送信する
    return pthread_kill(tid, (int)irq);
}

// 割り込み処理スレッド。割り込みスレッドのエントリポイントとなる。
static void * intr_thread(void *arg)
{
    int terminate = 0, sig, err;
    struct irq_entry *entry;

    debugf("barrier waiting in intr_thread()");
    pthread_barrier_wait(&barrier); // メインスレッドと同期を取る
    debugf("barrier ok. Start interrupt in intr_thread()");

    // 割り込みがくるまで待機(ユーザー空間で動くので実際は割り込み = シグナル)
    while (!terminate) {
        err = sigwait(&sigmask, &sig);
        if (err) {
            errorf("sigwait() %s", strerror(err));
            break;
        }

        switch (sig) {
        case SIGHUP:
            debugf("Interrupt by SIGHUP");
            // 割り込みスレッドに終了が通知されたら terminate = 1 にして抜ける
            terminate = 1;
            break;
        default:
            debugf("Interrupt. finding handler...");
            // IRQ リストを巡回して一致するエントリの割り込みハンドラを呼び出す
            for (entry = irqs; entry; entry = entry->next) {
                if (entry->irq == (unsigned int)sig) {
                    debugf("found IRQ. irq=%d, name=%s", entry->irq, entry->name);
                    entry->handler(entry->irq, entry->dev);
                }
            }

            debugf("handler call finished.");
            break;
        }
    }

    return NULL;
}

// 割り込み機構の起動
int intr_run(void)
{
    int err;

    // pthread_sigmask() で禁止するシグナルマスクの確認と変更を行う
    // ここでは sigmask (シグナルの集合が入っている)を禁止する
    err = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
    if (err) {
        errorf("pthread_sigmask() %s", strerror(err));
        return -1;
    }
    // 割り込みスレッドを起動する
    err = pthread_create(&tid, NULL, intr_thread, NULL);
    if (err) {
        errorf("pthread_create() %s", strerror(err));
        return -1;
    }

    // スレッドが動き出すまで待機する
    // 他のスレッドが pthread_barrier_wait() を呼び出してバリアのカウントが指定の数(2)になるまでスレッドを停止する
    debugf("barrier waiting in intr_run()");
    pthread_barrier_wait(&barrier);
    return 0;
}
void intr_shutdown(void)
{
    // 割り込み処理スレッドが起動済みか確認する
    if (pthread_equal(tid, pthread_self()) != 0) {
        return;
    }

    pthread_kill(tid, SIGHUP); // 割り込み処理スレッドにシグナルを送信
    pthread_join(tid, NULL); // 割り込み処理スレッドが完全に終了するまで待機
}

// 割り込み機構の初期化
int intr_init(void)
{
    tid = pthread_self();
    pthread_barrier_init(&barrier, NULL, 2);
    // シグナルの集合を空にしておく
    sigemptyset(&sigmask);
    // 割り込みスレッド終了通知用として SIGHUP を登録
    sigaddset(&sigmask, SIGHUP);

    return 0;
}