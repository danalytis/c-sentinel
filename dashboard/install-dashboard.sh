#!/bin/bash
#
# C-Sentinel Dashboard Installation Script
#

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}C-Sentinel Dashboard Installer${NC}"
echo "================================="
echo

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Please run as root (sudo ./install-dashboard.sh)${NC}"
    exit 1
fi

# Configuration
INSTALL_DIR="/opt/sentinel-dashboard"
DB_PASSWORD="${DB_PASSWORD:-your-secure-password}"
API_KEY="${API_KEY:-$(openssl rand -hex 16)}"

echo -e "${YELLOW}Installing to: ${INSTALL_DIR}${NC}"
echo -e "${YELLOW}API Key: ${API_KEY}${NC}"
echo
echo "Save the API key - you'll need it to configure sentinel agents!"
echo

# Create installation directory
echo -e "${YELLOW}Creating installation directory...${NC}"
mkdir -p "$INSTALL_DIR"
cp -r . "$INSTALL_DIR/"
cd "$INSTALL_DIR"

# Create virtual environment
echo -e "${YELLOW}Creating Python virtual environment...${NC}"
python3 -m venv venv
./venv/bin/pip install --upgrade pip
./venv/bin/pip install -r requirements.txt

# Update service file with actual password and API key
echo -e "${YELLOW}Configuring service...${NC}"
sed -i "s/your-secure-password/${DB_PASSWORD}/g" sentinel-dashboard.service
sed -i "s/change-me-in-production/${API_KEY}/g" sentinel-dashboard.service

# Install systemd service
cp sentinel-dashboard.service /etc/systemd/system/
systemctl daemon-reload

# Install nginx config
echo -e "${YELLOW}Installing nginx configuration...${NC}"
cp nginx-sentinel.conf /etc/nginx/sites-available/sentinel.speytech.com
ln -sf /etc/nginx/sites-available/sentinel.speytech.com /etc/nginx/sites-enabled/

# Test nginx config
nginx -t

# Initialize database
echo -e "${YELLOW}Initializing database...${NC}"
cd "$INSTALL_DIR"
DB_HOST=localhost DB_PORT=5432 DB_NAME=sentinel DB_USER=sentinel DB_PASSWORD="$DB_PASSWORD" \
    ./venv/bin/python -c "from app import init_db; init_db()"

echo
echo -e "${GREEN}Installation complete!${NC}"
echo
echo "Next steps:"
echo "  1. Get SSL certificate:"
echo "     sudo certbot --nginx -d sentinel.speytech.com"
echo
echo "  2. Start the dashboard:"
echo "     sudo systemctl enable sentinel-dashboard"
echo "     sudo systemctl start sentinel-dashboard"
echo
echo "  3. Reload nginx:"
echo "     sudo systemctl reload nginx"
echo
echo "  4. Configure sentinel agent to report:"
echo "     Add to /etc/sentinel/config:"
echo "       webhook_url = https://sentinel.speytech.com/api/ingest"
echo
echo "     Or use curl:"
echo "       ./bin/sentinel --json --network | curl -X POST \\"
echo "         -H 'Content-Type: application/json' \\"
echo "         -H 'X-API-Key: ${API_KEY}' \\"
echo "         -d @- https://sentinel.speytech.com/api/ingest"
echo
echo -e "${YELLOW}API Key: ${API_KEY}${NC}"
echo "(Save this - you'll need it for agent configuration)"
echo
