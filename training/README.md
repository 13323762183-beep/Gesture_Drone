# TinyCNN 训练与导出

先在本目录安装依赖：

```powershell
python -m pip install -r ..\requirements-training.txt
```

## 公开数据训练

将数据放入 `input/fist`、`input/palm`、`input/peace`，并把 `minimal-annotations.json` 放到 `input`。双击 `run_training.bat`。

脚本先生成 `output/mask_review.png`；确认手部是合理白色主体后按任意键继续训练、导出和自检。输出模型、指标与板端头文件位于 `output/model` 和 `output/deploy`。

## 加入板端采集掩膜微调

把板端保存的掩膜分别放入 `input_board/V`、`input_board/OPEN`、`input_board/FIST`。公开数据训练完成后双击 `run_board_finetune.bat`，输出位于 `output_board`。

工具只训练 V / OPEN / FIST；无手不作为 CNN 类别，而由运行时前景面积阈值判断。`final_model` 是当前部署版本，其权重与 AX7020 SDK 源码一致。
