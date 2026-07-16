# Turtle Soup Cases

Local turtle soup case bank for Tee探长.

Sources used:

- https://huggingface.co/datasets/nycu-ai113-dl-final-project/TurtleBench-extended-zh
  - Dataset card license: Apache-2.0
  - Converted rows with `title`, `surface`, `bottom`, `user_guess`, `label` into local case records.
- https://huggingface.co/datasets/Duguce/TurtleBench1.5k
  - Dataset card/license: Apache-2.0
  - Imported Chinese `staging/stories.json` and aggregated labels from `zh_data-00000-of-00001.jsonl`.
- https://github.com/wangyafu/haiguitangmcp
  - Imported 35 public curated puzzle markdown files for this private server setup.
  - This repository does not declare an explicit content license. Keep this source separate if redistributing a public package.
  - The repository README says these puzzles were collected for hgtang.com and includes a recommended high-rated list. Those recommended titles are marked with `quality: recommended`.

Case count: 623

Text normalization: converted to Simplified Chinese with OpenCC t2s and deduplicated after conversion.

Runtime selection: `turtle_soup.py` keeps the full JSON bank but starts games from a filtered playable pool. Curated/recommended puzzles are preferred over noisy generated expansion rows.
