// @flow
import React from 'react'
import { Controlled as OrigCodeMirror } from 'react-codemirror2'

import styles from './style.css'

import 'codemirror/lib/codemirror.css'
import 'codemirror/theme/material.css'
import 'codemirror/theme/material.css'

import 'codemirror/mode/clike/clike'
import 'codemirror/mode/d/d'
import 'codemirror/mode/ruby/ruby'
import 'codemirror/mode/python/python'
import 'codemirror/mode/perl/perl'
import 'codemirror/mode/erlang/erlang'
import 'codemirror/mode/haskell/haskell'
import 'codemirror/mode/shell/shell'
import 'codemirror/mode/lua/lua'
import 'codemirror/mode/php/php'
import 'codemirror/mode/commonlisp/commonlisp'
import 'codemirror/mode/pascal/pascal'
import 'codemirror/mode/rust/rust'
import 'codemirror/mode/groovy/groovy'
import 'codemirror/mode/javascript/javascript'
import 'codemirror/mode/coffeescript/coffeescript'
import 'codemirror/mode/swift/swift'
import 'codemirror/mode/mllike/mllike'
import 'codemirror/mode/go/go'
import 'codemirror-mode-elixir'

import 'codemirror/keymap/vim'
import 'codemirror/keymap/emacs'

import 'codemirror/addon/search/searchcursor'
import 'codemirror/addon/edit/matchbrackets'
import 'codemirror/addon/dialog/dialog'

export type CodeMirrorType = Object
export type CodeMirrorOptions = {
  keyMap?: string,
  lineNumbers?: boolean,
  theme?: string,
  mode?: string,
  indentUnit?: number,
  indentWithTabs?: boolean,
  tabSize?: number,
  smartIndent?: boolean,
  extraKeys?: { [string]: (CodeMirrorType) => void },
  viewportMargin?: number
}

type Props = {
  value: string,
  options: CodeMirrorOptions,
  onBeforeChange: (CodeMirrorType, Object, string, () => void) => void,
  expand: boolean
}

export default function CodeMirror(props: Props) {
  return (
    <OrigCodeMirror
      className={props.expand ? styles.expand : null}
      value={props.value}
      options={props.options}
      onBeforeChange={props.onBeforeChange}
    />
  )
}
