import Lake
open System Lake DSL

package «jitter-buffer» where

require «truth_research_zk» from git
  "https://github.com/V-Sekai-fire/truth_research_zk.git" @ "add-i64-r128-emitters-fix-mapscalar"

lean_lib «JitterBuffer» where
  roots := #[`JitterBuffer]
