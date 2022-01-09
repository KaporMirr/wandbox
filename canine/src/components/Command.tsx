import React from "react";
import { useContainer } from "unstated-next";

import { reduceCompileOptions } from "~/utils/reduceCompileOptions";
import { CompilerList, CompilerInfo } from "~/hooks/compilerList";
import { CompilerContext } from "~/contexts/CompilerContext";
import { PermlinkData } from "~/hooks/permlink";

export interface CommandProps {
  compilerList: CompilerList;
  permlinkData: PermlinkData | null;
}

function rawToOptions(raw: string): string {
  return raw.split("\n").join(" ");
}

const Command: React.FC<CommandProps> = (props): React.ReactElement => {
  const { compilerList, permlinkData } = props;
  const compiler = useContainer(CompilerContext);

  const command = React.useMemo((): string => {
    let info: CompilerInfo;
    if (permlinkData === null) {
      const infoUndef = compilerList.compilers.find(
        (c): boolean => c.name === compiler.currentCompilerName
      );
      if (infoUndef === undefined) {
        return "";
      }
      info = infoUndef;
    } else {
      info = permlinkData.parameter.compilerInfo;
    }

    const command = info.displayCompileCommand;
    const options = reduceCompileOptions<string[]>(
      compiler.currentSwitches,
      info,
      [],
      (sw, state): string[] => [...state, sw.displayFlags],
      (sw, value, state): string[] => {
        const opt = sw.options.find((opt): boolean => opt.name === value);
        if (opt === undefined) {
          throw "something wrong";
        }
        return [...state, opt.displayFlags];
      }
    );
    const rawOptions = rawToOptions(
      info.compilerOptionRaw
        ? compiler.compilerOptionRaw
        : compiler.runtimeOptionRaw
    );
    return `$ ${command} ${options.join(" ")} ${rawOptions}`;
  }, [compiler, compilerList]);

  return <code>{command}</code>;
};

export { Command };
