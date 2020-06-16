# Canine

Wandbox のフロントエンドです。

## 方針

### 言語とライブラリ

TypeScript + React + Bootstrap で頑張る。

拡張子は .ts か .tsx。.ts で済むところを .tsx にしてても気にしないことにする。

### 推奨エディタ

VS Code。以下のプラグインを入れておく。

- Debugger for Chrome
- ESLint

このディレクトリをルートとして開けば、VS Code のプラグイン設定が入った状態で動作する。

### コンポーネントを Atomic Design っぽく分ける

`components/` 以下は Atomic Design みたいな感じで使う

- `atoms/` - Atomic Design の Atom と同じ意味
- `molecules/` - Atom やライブラリの組み合わせて汎用的に使えそうなコンポーネント。アプリケーションに依存しない
- `organisms/` - ページ内で利用するコンポーネント。アプリケーション依存で構わない。
- `pages/` - ルーターによって分けられた各ページのルート
- `top/`  - ルートコンポーネントや、ルーターによって分けられる前の全てのページで共通に使用されるコンポーネント

### default export

既存の default export されたライブラリを利用するのは問題ないが、こちらでは default export するスクリプトを書かないこと。
[eslint-plugin-import](https://github.com/benmosher/eslint-plugin-import) に default export を禁止するプラグインがあるが、依存を増やしたくないのでやめておく。

## TODO

公開まで必須なもの:

- [x] パーマリンク
- [x] パーマリンク時のreadonly化
- [x] パーマリンクから編集画面への遷移
- [x] 見た目を整える
- [x] エディタ設定の保存
- [x] コードの自動保存
- [x] デプロイの仕組みを作る
- [ ] 標準入力
- [ ] 出力ウィンドウの設定と保存
- [ ] テンプレートのロード
- [ ] GitHub連携を作る
- [ ] ユーザ用のページを作る
- [ ] Twitterカード
- [ ] タイトルと詳細の入力
- [ ] スポンサー表示

後でやりたいもの:

- [ ] i18n
- [ ] HEAD コンパイラのビルド日時
- [ ] お知らせ
- [ ] 標準入力とrawコンパイルオプションのバイナリ入力
- [ ] 各設定の保存/ロード
- [ ] 外部リンク（GitHub, エディタなど）
- [ ] コンパイラ要求用フォーム