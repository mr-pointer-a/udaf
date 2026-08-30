#!/bin/bash
# scripts/cron/auto_continue.sh - 每日 0:00 / 5:00 自动检查可继续任务
#
# 输出：写到 ~/.claude/cron_reports/，供下次会话启动时读取
#
# 设计依据：
#   - CronCreate 触发的命令没有 Claude session
#   - 在 cron 中直接执行 `claude` 调用会超时/无状态
#   - 改为：cron 仅做"清单输出 + 报告持久化"
#   - 下次 Claude 会话启动时由 user 触发 /continue 读取并执行

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
REPORT_DIR="$HOME/.claude/cron_reports"
mkdir -p "$REPORT_DIR"

TS="$(date +%Y%m%d_%H%M%S)"
REPORT="$REPORT_DIR/udaf_${TS}.md"

cat > "$REPORT" <<EOF
# UDAF 自动任务检查报告

**生成时间**: $(date -Iseconds)
**触发**: cron (0:00 / 5:00)
**项目目录**: $PROJECT_DIR

---

## 当前可继续任务清单

EOF

# 1) 从 memory 读取 P9 之后的进度
MEM="$HOME/.claude/projects/-data1-project-flibs-new/memory/project/udaf-impl-current-state.md"
if [ -f "$MEM" ]; then
    echo "### Memory 进度文件" >> "$REPORT"
    echo '```' >> "$REPORT"
    head -25 "$MEM" >> "$REPORT"
    echo '```' >> "$REPORT"
fi

# 2) git 状态
echo "" >> "$REPORT"
echo "## Git 状态" >> "$REPORT"
echo '```' >> "$REPORT"
cd "$PROJECT_DIR" && git status --short 2>&1 | head -20 >> "$REPORT"
echo '```' >> "$REPORT"

# 3) 测试状态（带超时保护）
echo "" >> "$REPORT"
echo "## 测试状态" >> "$REPORT"
if [ -d "$PROJECT_DIR/build-cov" ]; then
    echo '```' >> "$REPORT"
    timeout 60 ctest --test-dir "$PROJECT_DIR/build-cov" -L unit 2>&1 | tail -5 >> "$REPORT" || echo "(timeout)" >> "$REPORT"
    echo '```' >> "$REPORT"
fi

# 4) 覆盖率（简版）
echo "" >> "$REPORT"
echo "## 覆盖率（行）" >> "$REPORT"
if [ -f /tmp/cov_clean.info ]; then
    echo '```' >> "$REPORT"
    awk -F'[,:% ()]+' '/^SF:/{hit=0;tot=0} /^DA:/{tot++; if($3>0)hit++} /^end_of_record/{if(tot>0)total_hit+=hit; total_tot+=tot} END{printf "%.1f%% (%d/%d)\n", total_hit*100/total_tot, total_hit, total_tot}' /tmp/cov_clean.info >> "$REPORT"
    echo '```' >> "$REPORT"
fi

# 5) 推荐的继续任务（基于当前未完成项）
cat >> "$REPORT" <<EOF

## 建议的下一步

基于 udaf-impl-current-state.md，下一步可选：
- 覆盖率 90.6% → 92%+（补 13 个低覆盖文件）
- v0.3.0 准备：CHANGELOG 增项 + git tag
- 性能契约自动化校验脚本
- ASan/UBSan 全量回归验证

---

**下次 Claude 会话启动时，请输入 "继续" 读取本报告并执行。**
EOF

echo "Report saved: $REPORT"
