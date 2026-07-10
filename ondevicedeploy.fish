mv /home/arsrv/localsoftware /usr/local/bin/localsoftware

rm -rf /srv/landing
rm -rf /srv/bwith
rm -rf /srv/rp
rm -rf /var/log/wlan_monitor

mv /home/arsrv/client /srv/landing
mkdir -p /srv/bwith
mkdir -p /srv/rp
mkdir -p /etc/localsoftware

mv /home/arsrv/monitoringindex.html /srv/bwith/index.html
mv /home/arsrv/proxyindex.html /srv/rp/index.html

mv /home/arsrv/proxy.config /etc/localsoftware/proxy.config
mv /home/arsrv/localsoftware.service /etc/systemd/system/localsoftware.service
mv /home/arsrv/98-update-vpn /etc/NetworkManager/dispatcher.d/98-update-vpn;
    and chown root:root /etc/NetworkManager/dispatcher.d/98-update-vpn;
    and chmod +x /etc/NetworkManager/dispatcher.d/98-update-vpn
mv /home/arsrv/99-update-dnsmasq-relay /etc/NetworkManager/dispatcher.d/99-update-dnsmasq-relay;
    and chown root:root /etc/NetworkManager/dispatcher.d/99-update-dnsmasq-relay;
    and chmod +x /etc/NetworkManager/dispatcher.d/99-update-dnsmasq-relay

mkdir -p /var/log/localsoftware;
    and chown arsrv:arsrv /var/log/localsoftware

setcap 'cap_net_bind_service=+ep' /usr/local/bin/localsoftware

restorecon /usr/local/bin/localsoftware; or echo "Failed to set permissions: exiting"
restorecon /etc/systemd/system/localsoftware.service; or echo "Failed to set permissions: exiting"
restorecon -r /srv; or echo "Failed to set permissions: exiting"
restorecon -r /etc/NetworkManager/dispatcher.d; or echo "Failed to set permissions: exiting"

systemctl daemon-reload
systemctl restart localsoftware.service
