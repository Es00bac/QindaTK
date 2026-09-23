// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Financial and target displays"
    note: "CandlestickView · TargetDisplay · explicit sessions and thresholds"
    Tk.OhlcModel { id: prices }
    Component.onCompleted: prices.replaceCandles([
        {id: "a", utcMs: "1", open: 10, high: 15, low: 9, close: 14},
        {id: "b", utcMs: "2", open: 14, high: 16, low: 11, close: 12},
        {id: "gap", utcMs: "3", missing: true},
        {id: "c", utcMs: "4", open: 12, high: 18, low: 11, close: 17}
    ], "USD/share", "Example exchange session", "UTC", 13)
    Tk.CandlestickView {
        width: 940
        height: 240
        candleModel: prices
        Tk.Flex.shrink: 0
    }
}
