/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <syslog.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <signal.h>

#include <nvram/bcmnvram.h>
#include <shutils.h>
#include <ralink.h>

#include "rc.h"

static char udhcp_state[16] = {0};

void 
reset_wan_vars(int full_reset)
{
	char macbuf[36];
	
	if (full_reset)
	{
		nvram_set("wan_ifname_t", "");
	}
	
	nvram_set("l2tp_cli_t", "0");
	nvram_set("wan_status_t", "Disconnected");
	nvram_unset("wan_ready");
	
	nvram_unset("wanx_ipaddr"); 
	nvram_unset("wanx_netmask");
	nvram_unset("wanx_gateway");
	nvram_unset("wanx_dns");
	nvram_unset("wanx_lease");
	
	nvram_set("wan0_proto", nvram_safe_get("wan_proto"));
	
	if (nvram_match("x_DHCPClient", "0") || nvram_match("wan_proto", "static"))
	{
		nvram_set("wan0_ipaddr", nvram_safe_get("wan_ipaddr"));
		nvram_set("wan0_netmask", nvram_safe_get("wan_netmask"));
		nvram_set("wan0_gateway", nvram_safe_get("wan_gateway"));
	}
	else
	{
		nvram_set("wan0_ipaddr", "0.0.0.0");
		nvram_set("wan0_netmask", "0.0.0.0");
		nvram_set("wan0_gateway", "0.0.0.0");
	}
	
	nvram_set("wan_ipaddr_t", "");
	nvram_set("wan_netmask_t", "");
	nvram_set("wan_gateway_t", "");
	nvram_set("wan_dns_t", "");
	nvram_set("wan_subnet_t", "");
	
	wan_netmask_check();
	
	if (nvram_match("wan_proto", "pppoe") || nvram_match("wan_proto", "pptp") || nvram_match("wan_proto", "l2tp"))
	{
		nvram_set("wan0_pppoe_ifname", "ppp0");
		nvram_set("wan0_pppoe_username", nvram_safe_get("wan_pppoe_username"));
		nvram_set("wan0_pppoe_passwd", nvram_safe_get("wan_pppoe_passwd"));
		if (nvram_match("wan_proto", "pppoe"))
			nvram_set("wan0_pppoe_idletime", nvram_safe_get("wan_pppoe_idletime"));
		else
			nvram_set("wan0_pppoe_idletime", "0");
		nvram_set("wan0_pppoe_txonly_x", nvram_safe_get("wan_pppoe_txonly_x"));
		nvram_set("wan0_pppoe_mtu", nvram_safe_get("wan_pppoe_mtu"));
		nvram_set("wan0_pppoe_mru", nvram_safe_get("wan_pppoe_mru"));
		nvram_set("wan0_pppoe_service", nvram_safe_get("wan_pppoe_service"));
		nvram_set("wan0_pppoe_ac", nvram_safe_get("wan_pppoe_ac"));
		nvram_set("wan0_pppoe_options_x", nvram_safe_get("wan_pppoe_options_x"));
		nvram_set("wan0_pptp_options_x", nvram_safe_get("wan_pptp_options_x"));
		
		nvram_set("wan0_pppoe_ipaddr", nvram_safe_get("wan0_ipaddr"));
		nvram_set("wan0_pppoe_netmask", inet_addr_(nvram_safe_get("wan0_ipaddr")) && inet_addr_(nvram_safe_get("wan0_netmask")) ? nvram_safe_get("wan0_netmask") : NULL);
		nvram_set("wan0_pppoe_gateway", nvram_safe_get("wan0_gateway"));
		
		nvram_set("wanx_ipaddr", nvram_safe_get("wan0_ipaddr"));
	}
	
	nvram_set("wan0_hostname", nvram_safe_get("wan_hostname"));
	
	mac_conv("wan_hwaddr_x", -1, macbuf);
	if (!nvram_match("wan_hwaddr_x", "") && strcasecmp(macbuf, "FF:FF:FF:FF:FF:FF"))
	{
		nvram_set("wan_hwaddr", macbuf);
		nvram_set("wan0_hwaddr", macbuf);
	}
	else
	{
		nvram_set("wan_hwaddr", nvram_safe_get("il1macaddr"));
		nvram_set("wan0_hwaddr", nvram_safe_get("il1macaddr"));
	}
	
	nvram_set("wan0_dnsenable_x", nvram_safe_get("wan_dnsenable_x"));
	nvram_unset("wan0_dns");
	nvram_unset("wan0_wins");
	
	convert_routes();
	
#if defined (USE_IPV6)
	reset_wan6_vars();
#endif
}


void 
launch_wanx(char *wan_ifname, char *prefix, int unit, int wait_dhcpc, int use_zcip)
{
	char tmp[100];
	
	char *ip_addr = nvram_safe_get(strcat_r(prefix, "pppoe_ipaddr", tmp));
	char *netmask = nvram_safe_get(strcat_r(prefix, "pppoe_netmask", tmp));
	char *gateway = nvram_safe_get(strcat_r(prefix, "pppoe_gateway", tmp));
	char *pppname = nvram_safe_get(strcat_r(prefix, "pppoe_ifname", tmp));
	
	if (!(*netmask))
		netmask = NULL;
	
	/* Bring up physical WAN interface */
	ifconfig(wan_ifname, IFUP, ip_addr, netmask);
	
	if (inet_addr_(ip_addr) == INADDR_ANY)
	{
		/* PPPoE connection not needed WAN physical address first, skip wait DHCP lease */
		/* PPTP and L2TP needed WAN physical first for create VPN tunnel, wait DHCP lease */
		/* Start dhcpc daemon */
		if (!use_zcip)
		{
			start_udhcpc_wan(wan_ifname, unit, wait_dhcpc);
			
			/* add delay 2s after eth3 up: gethostbyname issue (L2TP/PPTP) */
			if (wait_dhcpc)
				sleep(2);
		}
		else
			start_zcip_wan(wan_ifname);
	}
	else
	{
		/* start firewall */
		start_firewall_ex(pppname, "0.0.0.0");
		
		/* setup static wan routes via physical device */
		add_routes("wan_", "route", wan_ifname);
		
		/* and set default route if specified with metric 1 */
		if ( inet_addr_(gateway) != INADDR_ANY )
		{
			route_add(wan_ifname, 2, "0.0.0.0", gateway, "0.0.0.0");
		}
		
		/* start multicast router */
		start_igmpproxy(wan_ifname);
	}
	
#if defined (USE_IPV6)
	if (is_wan_ipv6_type_sit() == 0 && !is_wan_ipv6_if_ppp())
		wan6_up(wan_ifname);
#endif
}

void
start_wan(void)
{
	char *wan_ifname, *ppp_ifname;
	char *wan_proto;
	int unit, sockfd;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	struct ifreq ifr;
	int is_pppoe;
	
	/* check if we need to setup WAN */
	if (nvram_match("router_disable", "1"))
	{
		return;
	}
	
	wan_mac_config();
	
	reload_nat_modules();
	
	update_wan_status(0);
	
	/* Create links */
	mkdir("/tmp/ppp", 0777);
	mkdir("/tmp/ppp/peers", 0777);
	
	symlink("/sbin/rc", "/tmp/ppp/ip-up");
	symlink("/sbin/rc", "/tmp/ppp/ip-down");
	symlink("/sbin/rc", SCRIPT_UDHCPC_WAN);
	symlink("/sbin/rc", SCRIPT_ZCIP_WAN);
	symlink("/sbin/rc", SCRIPT_WPACLI_WAN);
#if defined (USE_IPV6)
	symlink("/sbin/rc", "/tmp/ppp/ipv6-up");
	symlink("/sbin/rc", "/tmp/ppp/ipv6-down");
	symlink("/sbin/rc", SCRIPT_DHCP6C_WAN);
#endif
	
	update_resolvconf(1, 0);
	
	smart_restart_upnp();
	
	/* Start each configured and enabled wan connection and its undelying i/f */
	for (unit = 0; unit < 2; unit ++) 
	{
		if (unit > 0 && !nvram_match("wan_proto", "pppoe")) 
			break;

		snprintf(prefix, sizeof(prefix), "wan%d_", unit);

		/* make sure the connection exists and is enabled */ 
		wan_ifname = nvram_get(strcat_r(prefix, "ifname", tmp));
		if (!wan_ifname) {
			continue;
		}
		wan_proto = nvram_get(strcat_r(prefix, "proto", tmp));
		if (!wan_proto || !strcmp(wan_proto, "disabled"))
			continue;
		
		is_pppoe = !strcmp(wan_proto, "pppoe");

		dbg("%s: wan_ifname=%s, wan_proto=%s\n", __FUNCTION__, wan_ifname, wan_proto);

		/* Bring up if */
		ifconfig(wan_ifname, IFUP, NULL, NULL);

		if (unit == 0) 
		{
			set_ip_forward();
			set_pppoe_passthrough();
		}

		/* 
		* Configure PPPoE connection. The PPPoE client will run 
		* ip-up/ip-down scripts upon link's connect/disconnect.
		*/
		
		if(get_usb_modem_state())
		{
			if(nvram_match("modem_enable", "4"))
			{
				char *rndis_ifname = nvram_safe_get("rndis_ifname");
				if (strlen(rndis_ifname) > 0) {
					ifconfig(rndis_ifname, IFUP, "0.0.0.0", NULL);
					start_udhcpc_wan(rndis_ifname, unit, 0);
					nvram_set("wan_ifname_t", rndis_ifname);
				}
			}
			else
			{
				if (is_wan_ppp(wan_proto))
				{
					if (!is_pppoe || nvram_match("pppoe_dhcp_route", "1"))
						launch_wanx(wan_ifname, prefix, unit, 0, 0);
					else if (is_pppoe && nvram_match("pppoe_dhcp_route", "2"))
						launch_wanx(wan_ifname, prefix, unit, 0, 1);
				}
				else
				{
					/* start firewall */
					start_firewall_ex("ppp0", "0.0.0.0");
					
					/* setup static wan routes via physical device */
					add_routes("wan_", "route", wan_ifname);
					
					/* start multicast router */
					start_igmpproxy(wan_ifname);
				}
				
				if (create_pppd_script_modem_3g())
				{
					/* launch pppoe client daemon */
					logmessage("start_wan()", "select 3G modem node %s to pppd", nvram_safe_get("modem_node_t"));
					
					eval("pppd", "call", "3g");
				}
				else
				{
					logmessage("start_wan()", "unable to open 3G modem script!");
				}
				
				nvram_set("wan_ifname_t", "ppp0");
			}
		}
		else
		if (is_wan_ppp(wan_proto))
		{
			if (!is_pppoe || nvram_match("pppoe_dhcp_route", "1"))
				launch_wanx(wan_ifname, prefix, unit, !is_pppoe, 0);
			else if (is_pppoe && nvram_match("pppoe_dhcp_route", "2"))
				launch_wanx(wan_ifname, prefix, unit, 0, 1);
			
			/* L2TP does not support idling */ // oleg patch
			int demand = nvram_get_int(strcat_r(prefix, "pppoe_idletime", tmp)) && strcmp(wan_proto, "l2tp");
			
			/* update demand option */
			nvram_set(strcat_r(prefix, "pppoe_demand", tmp), demand ? "1" : "0");
			
			/* set CPU load limit for prevent drop PPP session */
			set_ppp_limit_cpu();
			
			/* launch pppoe client daemon */
			start_pppd(prefix);
			
			/* ppp interface name is referenced from this point on */
			ppp_ifname = nvram_safe_get(strcat_r(prefix, "pppoe_ifname", tmp));
			
			/* Pretend that the WAN interface is up */
			if (nvram_match(strcat_r(prefix, "pppoe_demand", tmp), "1")) 
			{
				int timeout = 5;
				/* Wait for pppx to be created */
				while (ifconfig(ppp_ifname, IFUP, NULL, NULL) && timeout--)
					sleep(1);
				
				/* Retrieve IP info */
				if ((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) >= 0)
				{
					strncpy(ifr.ifr_name, ppp_ifname, IFNAMSIZ);
					
					/* Set temporary IP address */
					if (ioctl(sockfd, SIOCGIFADDR, &ifr) == 0)
					{
						nvram_set(strcat_r(prefix, "ipaddr", tmp), inet_ntoa(sin_addr(&ifr.ifr_addr)));
						nvram_set(strcat_r(prefix, "netmask", tmp), "255.255.255.255");
						
						/* Set temporary P-t-P address */
						if (ioctl(sockfd, SIOCGIFDSTADDR, &ifr) == 0)
						{
							nvram_set(strcat_r(prefix, "gateway", tmp), inet_ntoa(sin_addr(&ifr.ifr_dstaddr)));
						}
					}
					
					close(sockfd);
				}
				
				/* 
				* Preset routes so that traffic can be sent to proper pppx even before 
				* the link is brought up.
				*/
				preset_wan_routes(ppp_ifname);
			}
			
			nvram_set("wan_ifname_t", ppp_ifname);
		}
		
		/* 
		* Configure DHCP connection. The DHCP client will run 
		* 'udhcpc bound'/'udhcpc deconfig' upon finishing IP address 
		* renew and release.
		*/
		else if (strcmp(wan_proto, "dhcp") == 0)
		{
			/* Start eapol-md5 authenticator */
			if (nvram_match("wan_auth_mode", "2"))
				start_auth_eapol(wan_ifname);
			
			/* Start dhcp daemon */
			start_udhcpc_wan(wan_ifname, unit, 0);
			nvram_set("wan_ifname_t", wan_ifname);
#if defined (USE_IPV6)
			if (is_wan_ipv6_type_sit() == 0)
				wan6_up(wan_ifname);
#endif
		}
		/* Configure static IP connection. */
		else if ((strcmp(wan_proto, "static") == 0)) 
		{
			/* Assign static IP address to i/f */
			ifconfig(wan_ifname, IFUP,
				 nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)), 
				 nvram_safe_get(strcat_r(prefix, "netmask", tmp)));
			/* Start eapol-md5 authenticator */
			if (nvram_match("wan_auth_mode", "2"))
				start_auth_eapol(wan_ifname);
			
			/* We are done configuration */
			wan_up(wan_ifname);
			nvram_set("wan_ifname_t", wan_ifname);
#if defined (USE_IPV6)
			if (is_wan_ipv6_type_sit() == 0)
				wan6_up(wan_ifname);
#endif
		}
	}
}

void 
stop_wan_ppp()
{
	// stop services only for ppp0 interface
	char* svcs[] = { "l2tpd", 
	                 "xl2tpd", 
	                 "pppd", 
	                  NULL };
	
	kill_services(svcs, 6, 1);
	
	nvram_set("l2tp_cli_t", "0");
	nvram_set("wan_status_t", "Disconnected");
}

void
stop_wan(void)
{
	char *rndis_ifname;
	char *wan_ifname = IFNAME_WAN;
	char *svcs[] = { "ntpd", 
	                 "igmpproxy", 
	                 "udpxy", 
	                 "ip-up",
	                 "ip-down",
	                 "udhcpc",
	                 "zcip",
	                 "pppoe-relay",
#if defined (USE_IPV6)
	                 "ipv6-up",
	                 "ipv6-down",
#endif
	                 "l2tpd",
	                 "xl2tpd",
	                 "pppd",
	                 "detect_wan",
	                  NULL };
	
#if defined (USE_IPV6)
	if (is_wan_ipv6_type_sit() == 0)
		wan6_down(wan_ifname);
#endif
	if (pids("udhcpc"))
	{
		logmessage("stop_wan()", "raise DHCP release event");
		system("killall -SIGUSR2 udhcpc");
		usleep(50000);
	}
	
	stop_auth_eapol();
	stop_auth_kabinet();
	disable_all_passthrough();
	
	kill_services(svcs, 6, 1);
	
	if (!is_physical_wan_dhcp() && nvram_match("wan_ifname_t", wan_ifname))
		wan_down(wan_ifname);
	
	/* Bring down WAN interfaces */
	ifconfig(wan_ifname, 0, "0.0.0.0", NULL);
	
	/* Bring down usbnet interface */
	rndis_ifname = nvram_safe_get("rndis_ifname");
	if (strlen(rndis_ifname) > 0) {
		ifconfig(rndis_ifname, 0, "0.0.0.0", NULL);
	}
	
	/* Remove dynamically created links */
	unlink(SCRIPT_ZCIP_WAN);
	unlink(SCRIPT_UDHCPC_WAN);
	unlink(SCRIPT_WPACLI_WAN);
	unlink("/tmp/ppp/ip-up");
	unlink("/tmp/ppp/ip-down");
	unlink("/tmp/ppp/link.ppp0");
#if defined (USE_IPV6)
	unlink(SCRIPT_DHCP6C_WAN);
	unlink("/tmp/ppp/ipv6-up");
	unlink("/tmp/ppp/ipv6-down");
#endif
	flush_conntrack_caches();
	
	nvram_set("l2tp_cli_t", "0");
	
	update_wan_status(0);
}


void
stop_wan_static(void)
{
	char *wan_ifname = IFNAME_WAN;
	char *svcs[] = { "ntpd",
	                 "udhcpc",
	                 "zcip",
	                 "pppoe-relay",
	                 "l2tpd",
	                 "xl2tpd",
	                 "pppd",
	                 "igmpproxy", // oleg patch
	                 "udpxy",
	                  NULL };
	
#if defined (USE_IPV6)
	if (is_wan_ipv6_type_sit() == 0)
		wan6_down(wan_ifname);
#endif
	if (pids("udhcpc"))
	{
		logmessage("stop_wan_static()", "raise DHCP release event");
		system("killall -SIGUSR2 udhcpc");
		usleep(50000);
	}
	
	stop_auth_eapol();
	stop_auth_kabinet();
	disable_all_passthrough();
	
	kill_services(svcs, 5, 1);
	
	
	if (nvram_match("wan_ifname_t", wan_ifname))
		wan_down(wan_ifname);
	
	/* Remove dynamically created links */
	unlink(SCRIPT_ZCIP_WAN);
	unlink(SCRIPT_UDHCPC_WAN);
	unlink(SCRIPT_WPACLI_WAN);
	unlink("/tmp/ppp/ip-up");
	unlink("/tmp/ppp/ip-down");
#if defined (USE_IPV6)
	unlink(SCRIPT_DHCP6C_WAN);
	unlink("/tmp/ppp/ipv6-up");
	unlink("/tmp/ppp/ipv6-down");
#endif
	
	flush_conntrack_caches();
}


void
wan_up(char *wan_ifname)
{
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	char *wan_proto, *gateway;
	char *script_postw = "/etc/storage/post_wan_script.sh";
	int  is_modem_unit = is_ifunit_modem(wan_ifname);
	
	logmessage(LOGNAME, "wan up (%s)", wan_ifname);
	
	/* Figure out nvram variable name prefix for this i/f */
	if (wan_prefix(wan_ifname, prefix) < 0)
	{
		/* called for dhcp+ppp */
		if (!nvram_match("wan0_ifname", wan_ifname))
		{
			return;
		}
		
		/* re-start firewall with old ppp0 address or 0.0.0.0 */
		start_firewall_ex("ppp0", nvram_safe_get("wan0_ipaddr"));
		
		/* setup static wan routes via physical device */
		add_routes("wan_", "route", wan_ifname);
		
		/* and one supplied via DHCP */
		add_wanx_routes("wanx_", wan_ifname, 0);
		
		gateway = nvram_safe_get("wanx_gateway");
		
		/* and default route with metric 1 */
		if (inet_addr_(gateway) != INADDR_ANY)
		{
			char word[100], *next;
			in_addr_t addr = inet_addr(nvram_safe_get("wanx_ipaddr"));
			in_addr_t mask = inet_addr(nvram_safe_get("wanx_netmask"));
			
			/* if the gateway is out of the local network */
			if ((inet_addr(gateway) & mask) != (addr & mask))
				route_add(wan_ifname, 2, gateway, NULL, "255.255.255.255");
			
			/* default route via default gateway */
			route_add(wan_ifname, 2, "0.0.0.0", gateway, "0.0.0.0");
			
			/* ... and to dns servers as well for demand ppp to work */
			if (nvram_match("wan_dnsenable_x", "1") && nvram_invmatch("wan_proto", "pppoe"))
			{
				foreach(word, nvram_safe_get("wanx_dns"), next)
				{
					if ((inet_addr(word) != inet_addr(gateway)) && (inet_addr(word) & mask) != (addr & mask))
						route_add(wan_ifname, 2, word, gateway, "255.255.255.255");
				}
			}
		}
		
		/* start multicast router */
		start_igmpproxy(wan_ifname);
		
		update_resolvconf(0, 0);
		
		return;
	}
	
	wan_proto = nvram_safe_get(strcat_r(prefix, "proto", tmp));
	
	dprintf("%s %s\n", wan_ifname, wan_proto);
	
	/* Set default route to gateway if specified */
	if (nvram_match(strcat_r(prefix, "primary", tmp), "1"))
	{
		gateway = nvram_safe_get(strcat_r(prefix, "gateway", tmp));
		
		if ( (!is_modem_unit) && (strcmp(wan_proto, "dhcp") == 0 || strcmp(wan_proto, "static") == 0) )
		{
			/* the gateway is in the local network */
			route_add(wan_ifname, 0, gateway, NULL, "255.255.255.255");
		}
		
		/* default route via default gateway */
		route_add(wan_ifname, 0, "0.0.0.0", gateway, "0.0.0.0");
		
		/* hack: avoid routing cycles, when both peer and server has the same IP */
		if ( (!is_modem_unit) && (strcmp(wan_proto, "pptp") == 0 || strcmp(wan_proto, "l2tp") == 0)) {
			/* delete gateway route as it's no longer needed */
			route_del(wan_ifname, 0, gateway, "0.0.0.0", "255.255.255.255");
		}
	}
	
	/* Install interface dependent static routes */
	add_wan_routes(wan_ifname);
	
	/* Add static wan routes */
	if ( (!is_modem_unit) && (strcmp(wan_proto, "dhcp") == 0 || strcmp(wan_proto, "static") == 0) )
	{
		nvram_set("wanx_gateway", nvram_safe_get(strcat_r(prefix, "gateway", tmp)));
		add_routes("wan_", "route", wan_ifname);
	}
	
	/* Add dynamic routes supplied via DHCP */
	if ( ((!is_modem_unit) && (strcmp(wan_proto, "dhcp") == 0)) || (is_modem_unit == 2) )
	{
		add_wanx_routes(prefix, wan_ifname, 0);
	}
	
#if defined (USE_IPV6)
	if (is_wan_ipv6_type_sit() == 1)
		wan6_up(wan_ifname);
#endif
	
	/* Add dns servers to resolv.conf */
	update_resolvconf(0, 0);
	
	/* Start kabinet authenticator */
	if ( (!is_modem_unit) && (strcmp(wan_proto, "dhcp") == 0 || strcmp(wan_proto, "static") == 0) )
	{
		if (nvram_match("wan_auth_mode", "1"))
			start_auth_kabinet();
	}
	
	/* Sync time */
	update_wan_status(1);
	
	start_firewall_ex(wan_ifname, nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)));
	
	update_upnp(1);
	
	/* start multicast router */
	if ( (!is_modem_unit) && (strcmp(wan_proto, "dhcp") == 0 || strcmp(wan_proto, "static") == 0) )
	{
		start_igmpproxy(wan_ifname);
	}
	
	notify_watchdog_ddns();
	notify_watchdog_time();
	
	if ( (!is_modem_unit) && (strcmp(wan_proto, "dhcp") == 0) )
	{
		if (nvram_match("gw_arp_ping", "1") && !pids("detect_wan"))
		{
			eval("detect_wan");
		}
	}
	
	if (check_if_file_exist(script_postw))
	{
		doSystem("%s %s %s", script_postw, "up", wan_ifname);
	}
}

void
wan_down(char *wan_ifname)
{
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	char *wan_proto;
	char *script_postw = "/etc/storage/post_wan_script.sh";
	int  is_modem_unit = is_ifunit_modem(wan_ifname);
	
	logmessage(LOGNAME, "wan down (%s)", wan_ifname);
	
	/* Figure out nvram variable name prefix for this i/f */
	if (wan_prefix(wan_ifname, prefix) < 0)
	{
		// dhcp + ppp (wan_ifname=eth3/eth2.2)
		/* stop multicast router */
		stop_igmpproxy();
		
		return;
	}
	
#if defined (USE_IPV6)
	if (is_wan_ipv6_type_sit() == 1)
		wan6_down(wan_ifname);
#endif
	wan_proto = nvram_safe_get(strcat_r(prefix, "proto", tmp));
	
	if ( (!is_modem_unit) && (strcmp(wan_proto, "dhcp") == 0 || strcmp(wan_proto, "static") == 0) )
	{
		/* Stop multicast router */
		stop_igmpproxy();
		
		/* Stop kabinet authenticator */
		if (nvram_match("wan_auth_mode", "1"))
			stop_auth_kabinet();
	}
	
	/* Remove default route to gateway if specified */
	if (nvram_match(strcat_r(prefix, "primary", tmp), "1"))
		route_del(wan_ifname, 0, "0.0.0.0", 
			nvram_safe_get(strcat_r(prefix, "gateway", tmp)),
			"0.0.0.0");
	
	/* Remove interface dependent static routes */
	del_wan_routes(wan_ifname);
	
	/* Update resolv.conf -- leave as is if no dns servers left for demand to work */
	if (*nvram_safe_get("wanx_dns"))	// oleg patch
		nvram_unset(strcat_r(prefix, "dns", tmp));
	
	update_resolvconf(0, 0);
	
	if ( (!is_modem_unit) && (strcmp(wan_proto, "static")==0) )
	{
		ifconfig(wan_ifname, IFUP, "0.0.0.0", NULL);
	}
	
	update_wan_status(0);
	
	// cleanup
	nvram_set("wan_ipaddr_t", "");
	
	// flush conntrack caches
	flush_conntrack_caches();
	
	if (check_if_file_exist(script_postw))
	{
		doSystem("%s %s %s", script_postw, "down", wan_ifname);
	}
}

void 
full_restart_wan(void)
{
	stop_wan();

	del_lan_routes(IFNAME_BR);

	update_router_mode();
	reset_wan_vars(0);

	flush_route_caches();

	add_lan_routes(IFNAME_BR);

	switch_config_vlan(0);

	select_usb_modem_to_wan(0);

	start_wan();

#ifndef USE_RPL2TP
	/* restore L2TP server after L2TP client closed */
	if (nvram_match("l2tp_srv_t", "1") && !pids("xl2tpd"))
	{
		restart_xl2tpd();
	}
#endif
}

void 
try_wan_reconnect(int try_use_modem)
{
	stop_wan();

	reset_wan_vars(0);

	if (try_use_modem) {
		select_usb_modem_to_wan(0);
	}

	start_wan();

#ifndef USE_RPL2TP
	/* restore L2TP server after L2TP client closed */
	if (nvram_match("l2tp_srv_t", "1") && !pids("xl2tpd"))
	{
		restart_xl2tpd();
	}
#endif
}

int
update_resolvconf(int is_first_run, int do_not_notify)
{
	FILE *fp;
	char word[256], *next, *wan_dns;
	char *google_dns = "8.8.8.8";
	int total_dns = 0;
	int resolv_changed = 0;
	int dns_static = is_dns_static();
	
	fp = fopen("/etc/resolv.conf", "w+");
	if (fp)
	{
		if (dns_static)
		{
			if (is_first_run)
				resolv_changed = 1;
			
			if (nvram_invmatch("wan_dns1_x", "") && nvram_invmatch("wan_dns1_x", "0.0.0.0")) {
				fprintf(fp, "nameserver %s\n", nvram_safe_get("wan_dns1_x"));
				total_dns++;
			}
			
			if (nvram_invmatch("wan_dns2_x", "") && nvram_invmatch("wan_dns2_x", "0.0.0.0")) {
				fprintf(fp, "nameserver %s\n", nvram_safe_get("wan_dns2_x"));
				total_dns++;
			}
		}
		else if (!is_first_run)
		{
			if (strlen(nvram_safe_get("wan0_dns")))
				wan_dns = nvram_safe_get("wan0_dns");
			else
				wan_dns = nvram_safe_get("wanx_dns");
			
			foreach(word, wan_dns, next)
			{
				if (strcmp(word, "0.0.0.0"))
				{
					fprintf(fp, "nameserver %s\n", word);
					total_dns++;
				}
			}
		}
		
		if (total_dns < 1)
			fprintf(fp, "nameserver %s\n", google_dns);
		
#if defined (USE_IPV6)
		wan_dns = nvram_safe_get("wan0_dns6");
		foreach(word, wan_dns, next)
		{
			if (strlen(word) > 0)
			{
				fprintf(fp, "nameserver %s\n", word);
				if (is_first_run)
					resolv_changed = 1;
			}
		}
#endif
		fclose(fp);
	}
	
	if (is_first_run)
	{
		/* create md5 hash for resolv.conf */
		system("md5sum /etc/resolv.conf > /tmp/hashes/resolv_md5");
	}
	else
	{
		/* check and update hashes for resolv.conf */
		if (system("md5sum -cs /tmp/hashes/resolv_md5") != 0)
		{
			resolv_changed = 1;
			system("md5sum /etc/resolv.conf > /tmp/hashes/resolv_md5");
		}
	}
	
	/* notify dns relay server */
	if (resolv_changed && !do_not_notify)
	{
		restart_dns();
	}
	
	return 0;
}

int 
update_hosts(void)
{
	FILE *fp;
	int i, i_max, i_sdhcp;
	char dhcp_ip[32], dhcp_name[32], *sip, *sname;
	char *ipaddr;
	char *host_name_nbt;

	ipaddr = nvram_safe_get("lan_ipaddr");

	host_name_nbt = nvram_safe_get("computer_name");
	if (!host_name_nbt[0] || !is_valid_hostname(host_name_nbt))
		host_name_nbt = nvram_safe_get("productid");

	i_sdhcp = nvram_get_int("dhcp_static_x");
	i_max  = nvram_get_int("dhcp_staticnum_x");
	if (i_max > 64) i_max = 64;

	fp = fopen("/etc/hosts", "w+");
	if (fp) {
		fprintf(fp, "127.0.0.1 %s %s\n", "localhost.localdomain", "localhost");
		fprintf(fp, "%s my.router\n", ipaddr);
		fprintf(fp, "%s my.%s\n", ipaddr, nvram_safe_get("productid"));
		fprintf(fp, "%s %s\n", ipaddr, host_name_nbt);
		if (i_sdhcp == 1) {
			for (i = 0; i < i_max; i++) {
				sprintf(dhcp_ip, "dhcp_staticip_x%d", i);
				sprintf(dhcp_name, "dhcp_staticname_x%d", i);
				sip = nvram_safe_get(dhcp_ip);
				sname = nvram_safe_get(dhcp_name);
				if (inet_addr_(sip) != INADDR_ANY && inet_addr_(sip) != inet_addr_(ipaddr) && is_valid_hostname(sname))
				{
					fprintf(fp, "%s %s\n", sip, sname);
				}
			}
		}
		
#if defined (USE_IPV6)
		if (get_ipv6_type() != IPV6_DISABLED) {
			fprintf(fp, "::1 %s %s\n", "localhost.localdomain", "localhost");
			char addr6s[INET6_ADDRSTRLEN];
			char* lan_addr6_host = get_lan_addr6_host(addr6s);
			if (lan_addr6_host) {
				fprintf(fp, "%s my.router\n", lan_addr6_host);
				fprintf(fp, "%s my.%s\n", lan_addr6_host, nvram_safe_get("productid"));
				fprintf(fp, "%s %s\n", lan_addr6_host, host_name_nbt);
			}
		}
#endif
		fclose(fp);
	}

	return 0;
}

void
add_wanx_routes(char *prefix, char *ifname, int metric)
{
	char *routes, *tmp;
	char buf[30];
	struct in_addr mask;
	char *ipaddr, *gateway;
	int bits;
	char netmask[] = "255.255.255.255";
	
	if (!nvram_match("dr_enable_x", "1"))
		return;
	
	/* routes */
	routes = strdup(nvram_safe_get(strcat_r(prefix, "routes", buf)));
	for (tmp = routes; tmp && *tmp; )
	{
		ipaddr = strsep(&tmp, "/");
		gateway = strsep(&tmp, " ");
		if (gateway && inet_addr_(ipaddr) != INADDR_ANY) {
			route_add(ifname, metric + 1, ipaddr, gateway, netmask);
		}
	}
	free(routes);
	
	/* rfc3442 or ms classless static routes */
	routes = nvram_safe_get(strcat_r(prefix, "routes_rfc", buf));
	if (!*routes)
		routes = nvram_safe_get(strcat_r(prefix, "routes_ms", buf));
	routes = strdup(routes);
	for (tmp = routes; tmp && *tmp; )
	{
		ipaddr  = strsep(&tmp, "/");
		bits    = atoi(strsep(&tmp, " "));
		gateway = strsep(&tmp, " ");

		if (gateway && bits > 0 && bits <= 32)
		{
			mask.s_addr = htonl(0xffffffff << (32 - bits));
			strcpy(netmask, inet_ntoa(mask));
			route_add(ifname, metric + 1, ipaddr, gateway, netmask);
		}
	}
	free(routes);
}

int
add_wan_routes(char *wan_ifname)
{
	char prefix[] = "wanXXXXXXXXXX_";

	/* Figure out nvram variable name prefix for this i/f */
	if (wan_prefix(wan_ifname, prefix) < 0)
		return -1;

	return add_routes(prefix, "route", wan_ifname);
}

int
del_wan_routes(char *wan_ifname)
{
	char prefix[] = "wanXXXXXXXXXX_";

	/* Figure out nvram variable name prefix for this i/f */
	if (wan_prefix(wan_ifname, prefix) < 0)
		return -1;

	return del_routes(prefix, "route", wan_ifname);
}

void 
select_usb_modem_to_wan(int wait_modem_sec)
{
	int i;
	int is_modem_found = 0;
	int modem_type = nvram_get_int("modem_enable");
	
	// Check modem enabled
	if (modem_type > 0)
	{
		for (i=0; i<=wait_modem_sec; i++)
		{
			if (modem_type == 4)
			{
				if ( is_ready_modem_4g() )
				{
					is_modem_found = 1;
					break;
				}
			}
			else
			{
				if ( is_ready_modem_3g() )
				{
					is_modem_found = 1;
					break;
				}
			}
			
			if (i<wait_modem_sec)
				sleep(1);
		}
	}
	
	set_usb_modem_state(is_modem_found);
}

void safe_remove_usb_modem(void)
{
	char* svcs[] = { "pppd", NULL };
	
	if (!is_usb_modem_ready())
		return;
	
	if(nvram_match("modem_enable", "4")) 
	{
		if (get_usb_modem_state())
		{
			if (pids("udhcpc"))
			{
				system("killall -SIGUSR2 udhcpc");
				usleep(50000);
				
				system("killall udhcpc");
			}
		}
		
		stop_modem_4g();
	}
	else
	{
		if (get_usb_modem_state())
		{
			kill_services(svcs, 5, 1);
		}
		
		stop_modem_3g();
	}
	
	set_usb_modem_state(0);
}


int 
is_dns_static(void)
{
	if (get_usb_modem_state())
	{
		return 0; // force dynamic dns for ppp0/eth0
	}
	
	if (nvram_match("wan0_proto", "static"))
	{
		return 1; // always static dns for eth3/eth2.2
	}
	
	return !nvram_match("wan_dnsenable_x", "1"); // dynamic or static dns for ppp0 or eth3/eth2.2
}

int 
is_physical_wan_dhcp(void)
{
	if (nvram_match("wan_proto", "static"))
	{
		return 0;
	}
	
	if (nvram_match("wan_proto", "dhcp") || nvram_match("x_DHCPClient", "1"))
	{
		return 1;
	}
	
	return 0;
}


int 
is_wan_ppp(char *wan_proto)
{
	if (strcmp(wan_proto, "pppoe") == 0 || strcmp(wan_proto, "pptp") == 0 || strcmp(wan_proto, "l2tp") == 0)
	{
		return 1;
	}
	
	return 0;
}

void get_wan_ifname(char wan_ifname[16])
{
	char *ifname = IFNAME_WAN;
	char *wan_proto = nvram_safe_get("wan_proto");
	
	if(get_usb_modem_state()){
		if(nvram_match("modem_enable", "4"))
			ifname = nvram_safe_get("rndis_ifname");
		else
			ifname = "ppp0";
	}
	else
	if (is_wan_ppp(wan_proto))
	{
		ifname = "ppp0";
	}
	
	strcpy(wan_ifname, ifname);
}


void
wan_mac_config(void)
{
	if (nvram_invmatch("wan_hwaddr", ""))
		doSystem("ifconfig %s hw ether %s", IFNAME_WAN, nvram_safe_get("wan_hwaddr"));
	else
		doSystem("ifconfig %s hw ether %s", IFNAME_WAN, nvram_safe_get("lan_hwaddr"));
}

int
wan_prefix(char *ifname, char *prefix)
{
	int unit;
	
	if ((unit = wan_ifunit(ifname)) < 0)
		return -1;

	sprintf(prefix, "wan%d_", unit);
	return 0;
}


int
wan_ifunit(char *wan_ifname)
{
	int unit;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";

	if ((unit = ppp_ifunit(wan_ifname)) >= 0) {
		return unit;
	} else {
		if (strcmp(wan_ifname, nvram_safe_get("rndis_ifname")) == 0)
			return 0;
		
		for (unit = 0; unit < 2; unit ++) {
			snprintf(prefix, sizeof(prefix), "wan%d_", unit);
			if (nvram_match(strcat_r(prefix, "ifname", tmp), wan_ifname) &&
			    (nvram_match(strcat_r(prefix, "proto", tmp), "dhcp") ||
			     nvram_match(strcat_r(prefix, "proto", tmp), "static")))
				return unit;
		}
	}
	return -1;
}

int
preset_wan_routes(char *wan_ifname)
{
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";

	printf("preset wan routes [%s]\n", wan_ifname);

	/* Figure out nvram variable name prefix for this i/f */
	if (wan_prefix(wan_ifname, prefix) < 0)
		return -1;

	/* Set default route to gateway if specified */
	if (nvram_match(strcat_r(prefix, "primary", tmp), "1"))
	{
		route_add(wan_ifname, 0, "0.0.0.0", "0.0.0.0", "0.0.0.0");
	}

	/* Install interface dependent static routes */
	add_wan_routes(wan_ifname);
	return 0;
}

int
wan_primary_ifunit(void)
{
	int unit;
	char tmp[100], prefix[16];
	
	for (unit = 0; unit < 2; unit ++) {
		snprintf(prefix, sizeof(prefix), "wan%d_", unit);
		if (nvram_match(strcat_r(prefix, "primary", tmp), "1"))
			return unit;
	}

	return 0;
}

int
is_ifunit_modem(char *wan_ifname)
{
	if (get_usb_modem_state())
	{
		if (ppp_ifunit(wan_ifname) >= 0)
		{
			return 1;
		}
		
		if (strcmp(wan_ifname, nvram_safe_get("rndis_ifname")) == 0)
		{
			return 2;
		}
	}
	
	return 0;
}

int
has_wan_ip(int only_broadband_wan)
{
	if (get_wan_ipaddr(only_broadband_wan) != INADDR_ANY)
		return 1;
	
	return 0;
}

int
got_wan_ip()
{
	char *wan_ip = nvram_safe_get("wan_ipaddr_t");
	if (strcmp("", wan_ip) && strcmp("0.0.0.0", wan_ip))
		return 1;
	else
		return 0;
}

in_addr_t get_wan_ipaddr(int only_broadband_wan)
{
	char *ifname = IFNAME_WAN;

	if (nvram_match("wan_route_x", "IP_Bridged"))
		return INADDR_ANY;

	if(!only_broadband_wan && get_usb_modem_state()){
		if(nvram_match("modem_enable", "4"))
			ifname = nvram_safe_get("rndis_ifname");
		else
			ifname = "ppp0";
	} 
	else if (nvram_match("wan0_proto", "dhcp") || nvram_match("wan0_proto", "static"))
		ifname = IFNAME_WAN;
	else
		ifname = "ppp0";
	
	return get_ipv4_addr(ifname);
}

// return value: first bit is WAN port, second bit is USB Modem.
int is_phyconnected(void)
{
	int ret = 0;

	if(is_usb_modem_ready())
		ret += 1<<1;

	if(nvram_match("link_wan", "1"))
		ret += 1;

	return ret;
}

int
ppp0_as_default_route(void)
{
	int i, n, found;
	FILE *fp;
	unsigned int dest, mask;
	char buf[256], device[256];

	n = 0;
	found = 0;
	mask = 0;
	device[0] = '\0';

	fp = fopen("/proc/net/route", "r");
	if (fp)
	{
		while (fgets(buf, sizeof(buf), fp) != NULL)
		{
			if (++n == 1 && strncmp(buf, "Iface", 5) == 0)
				continue;

			i = sscanf(buf, "%255s %x %*s %*s %*s %*s %*s %x",
						device, &dest, &mask);

			if (i != 3)
				break;

			if (device[0] != '\0' && dest == 0 && mask == 0)
			{
				found = 1;
				break;
			}
		}

		fclose(fp);

		if (found && !strcmp("ppp0", device))
			return 1;
		else
			return 0;
	}

	return 0;
}

int
found_default_route(int only_broadband_wan)
{
	int i, n, found;
	FILE *fp;
	unsigned int dest, mask;
	char buf[256], device[256];
	n = 0;
	found = 0;
	mask = 0;
	device[0] = '\0';

	fp = fopen("/proc/net/route", "r");
	if (fp)
	{
		while (fgets(buf, sizeof(buf), fp) != NULL)
		{
			if (++n == 1 && strncmp(buf, "Iface", 5) == 0)
				continue;

			i = sscanf(buf, "%255s %x %*s %*s %*s %*s %*s %x",
						device, &dest, &mask);

			if (i != 3)
			{
				break;
			}

			if (device[0] != '\0' && dest == 0 && mask == 0)
			{
				found = 1;
				break;
			}
		}

		fclose(fp);

		if (found)
		{
			if(!only_broadband_wan && get_usb_modem_state()){
				if(nvram_match("modem_enable", "4")){
					if(!strcmp(nvram_safe_get("rndis_ifname"), device))
						return 1;
					else
						goto no_default_route;
				}
				else
				{
					if(!strcmp("ppp0", device))
						return 1;
					else
						goto no_default_route;
				}
			}
			else
			if (nvram_match("wan0_proto", "dhcp") || nvram_match("wan0_proto", "static"))
			{
				if (!strcmp(IFNAME_WAN, device))
					return 1;
				else
					goto no_default_route;
			}
			else
			{
				if (!strcmp("ppp0", device) || !strcmp(IFNAME_WAN, device))
					return 1;
				else
					goto no_default_route;
			}
		}
		else
			goto no_default_route;
	}

no_default_route:

	return 0;
}

void 
update_wan_status(int isup)
{
	char *proto;
	char dns_str[36];

	memset(dns_str, 0, sizeof(dns_str));
	proto = nvram_safe_get("wan_proto");

	if(get_usb_modem_state())
		nvram_set("wan_proto_t", "Modem");
	else
	if (!strcmp(proto, "static")) nvram_set("wan_proto_t", "Static");
	else if (!strcmp(proto, "dhcp")) nvram_set("wan_proto_t", "Automatic IP");
	else if (!strcmp(proto, "pppoe")) nvram_set("wan_proto_t", "PPPoE");
	else if (!strcmp(proto, "pptp")) nvram_set("wan_proto_t", "PPTP");
	else if (!strcmp(proto, "l2tp")) nvram_set("wan_proto_t", "L2TP");
	if (!isup)
	{
		nvram_set("wan_ipaddr_t", "");
		nvram_set("wan_netmask_t", "");
		nvram_set("wan_gateway_t", "");
		nvram_set("wan_subnet_t", "");
		nvram_set("wan_dns_t", "");
		nvram_set("wan_status_t", "Disconnected");
	}
	else
	{
		nvram_set("wan_ipaddr_t", nvram_safe_get("wan0_ipaddr"));
		nvram_set("wan_netmask_t", nvram_safe_get("wan0_netmask"));
		nvram_set("wan_gateway_t", nvram_safe_get("wan0_gateway"));

		char wan_gateway[16], wan_ipaddr[16], wan_netmask[16], wan_subnet[11];
		
		memset(wan_gateway, 0, 16);
		strcpy(wan_gateway, nvram_safe_get("wan0_gateway"));
		memset(wan_ipaddr, 0, 16);
		strcpy(wan_ipaddr, nvram_safe_get("wan0_ipaddr"));
		memset(wan_netmask, 0, 16);
		strcpy(wan_netmask, nvram_safe_get("wan0_netmask"));
		memset(wan_subnet, 0, 11);
		sprintf(wan_subnet, "0x%x", inet_network(wan_ipaddr)&inet_network(wan_netmask));
		nvram_set("wan_subnet_t", wan_subnet);
		
		if ( is_dns_static() )
		{
			if (nvram_invmatch("wan_dns1_x", ""))
				sprintf(dns_str, "%s", nvram_safe_get("wan_dns1_x"));
			
			if (nvram_invmatch("wan_dns2_x", ""))
				sprintf(dns_str, " %s", nvram_safe_get("wan_dns2_x"));
			
			nvram_set("wan_dns_t", dns_str);
		}
		else
		{
			nvram_set("wan_dns_t", nvram_safe_get("wan0_dns"));
		}
		
		nvram_set("wan_status_t", "Connected");
	}
}


static int
udhcpc_deconfig(char *wan_ifname, int is_zcip)
{
	char *client_info = (is_zcip) ? "ZeroConf WAN Client" : "DHCP WAN Client";
	
	int unit = wan_ifunit(wan_ifname);
	
	if ( (unit < 0) && (nvram_match("wan0_proto", "l2tp") || nvram_match("wan0_proto", "pptp")))
	{
		/* fix hang-up issue */
		logmessage("dhcp client", "skipping resetting IP address to 0.0.0.0");
	}
	else
	{
		ifconfig(wan_ifname, IFUP, "0.0.0.0", NULL);
		
		if (unit < 0)
		{
			nvram_set("wanx_ipaddr", "0.0.0.0");
		}
	}

	wan_down(wan_ifname);

	logmessage(client_info, "%s: lease is lost", udhcp_state);

	return 0;
}

static int
udhcpc_bound(char *wan_ifname)	// udhcpc bound here, also call wanup
{
	char *value;
	char tmp[100], prefix[16], route[32];
	int unit;
	int changed = 0;
	int gateway = 0;
	int lease_dur = 0;

	if ((unit = wan_ifunit(wan_ifname)) < 0) 
		strcpy(prefix, "wanx_");
	else
		snprintf(prefix, sizeof(prefix), "wan%d_", unit);

	if ((value = getenv("ip"))) {
		changed = nvram_invmatch(strcat_r(prefix, "ipaddr", tmp), value);
		nvram_set(strcat_r(prefix, "ipaddr", tmp), trim_r(value));
	}
	if ((value = getenv("subnet")))
		nvram_set(strcat_r(prefix, "netmask", tmp), trim_r(value));
        if ((value = getenv("router"))) {
		gateway = 1;
		nvram_set(strcat_r(prefix, "gateway", tmp), trim_r(value));
	}
	if ((value = getenv("dns")))
		nvram_set(strcat_r(prefix, "dns", tmp), trim_r(value));
	if ((value = getenv("wins")))
		nvram_set(strcat_r(prefix, "wins", tmp), trim_r(value));
	else
		nvram_set(strcat_r(prefix, "wins", tmp), "");

	nvram_set(strcat_r(prefix, "routes", tmp), getenv("routes"));
	nvram_set(strcat_r(prefix, "routes_ms", tmp), getenv("msstaticroutes"));
	nvram_set(strcat_r(prefix, "routes_rfc", tmp), getenv("staticroutes"));
#if 0
	if ((value = getenv("hostname")))
		sethostname(trim_r(value), strlen(value) + 1);
#endif
	if ((value = getenv("domain")))
		nvram_set(strcat_r(prefix, "domain", tmp), trim_r(value));
	if ((value = getenv("lease"))) {
		nvram_set(strcat_r(prefix, "lease", tmp), trim_r(value));
		lease_dur = atoi(value);
	}
	
#if defined (USE_IPV6)
	if ((value = getenv("ip6rd")))
		store_ip6rd_from_dhcp(value, prefix);
#endif
	
	if (!gateway) {
		foreach(route, nvram_safe_get(strcat_r(prefix, "routes_rfc", tmp)), value) {
			if (gateway) {
				nvram_set(strcat_r(prefix, "gateway", tmp), route);
				break;
			} else
				gateway = !strcmp(route, "0.0.0.0/0");
		}
	}
	
	if (changed && unit == 0)
		ifconfig(wan_ifname, IFUP, "0.0.0.0", NULL);
	
	ifconfig(wan_ifname, IFUP,
		 nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)),
		 nvram_safe_get(strcat_r(prefix, "netmask", tmp)));

	wan_up(wan_ifname);

	logmessage("DHCP WAN Client", "%s (%s), IP: %s, GW: %s, lease time: %d", 
		udhcp_state, 
		wan_ifname,
		nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)), 
		nvram_safe_get(strcat_r(prefix, "gateway", tmp)), lease_dur);
	
	return 0;
}

static int
zcip_bound(char *wan_ifname)
{
	char *value;
	char tmp[100], prefix[sizeof("wanXXXXXXXXXX_")];
	int changed = 0;
	
	strcpy(prefix, "wanx_");
	
	if ((value = getenv("ip"))) {
		changed = nvram_invmatch(strcat_r(prefix, "ipaddr", tmp), value);
		nvram_set(strcat_r(prefix, "ipaddr", tmp), trim_r(value));
	}
	
	nvram_set(strcat_r(prefix, "netmask", tmp), "255.255.0.0");
	nvram_set(strcat_r(prefix, "gateway", tmp), "");
	nvram_set(strcat_r(prefix, "dns", tmp), "");
	
	if (changed)
		ifconfig(wan_ifname, IFUP, "0.0.0.0", NULL);
	
	ifconfig(wan_ifname, IFUP,
		nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)),
		nvram_safe_get(strcat_r(prefix, "netmask", tmp)));
	
	wan_up(wan_ifname);
	
	logmessage("ZeroConf WAN Client", "%s (%s), IP: %s", 
		udhcp_state, 
		wan_ifname,
		nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)));
	
	return 0;
}


static int
udhcpc_renew(char *wan_ifname)
{
	char *value;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	int unit;
	int changed = 0;

	if ((unit = wan_ifunit(wan_ifname)) < 0)
		strcpy(prefix, "wanx_");
	else
		snprintf(prefix, sizeof(prefix), "wan%d_", unit);
	
	if (!(value = getenv("subnet")) || nvram_invmatch(strcat_r(prefix, "netmask", tmp), trim_r(value)))
		return udhcpc_bound(wan_ifname);
	if (!(value = getenv("router")) || nvram_invmatch(strcat_r(prefix, "gateway", tmp), trim_r(value)))
		return udhcpc_bound(wan_ifname);
	if ((value = getenv("ip")) && nvram_invmatch(strcat_r(prefix, "ipaddr", tmp), trim_r(value)))
		return udhcpc_bound(wan_ifname);
	
	if ((value = getenv("dns")) && nvram_invmatch(strcat_r(prefix, "dns", tmp), trim_r(value))) {
		nvram_set(strcat_r(prefix, "dns", tmp), trim_r(value));
		changed = 1;
	}

	if ((value = getenv("wins")))
		nvram_set(strcat_r(prefix, "wins", tmp), trim_r(value));
	else
		nvram_set(strcat_r(prefix, "wins", tmp), "");
#if 0
	if ((value = getenv("hostname")))
		sethostname(trim_r(value), strlen(value) + 1);
#endif
	if ((value = getenv("domain")))
		nvram_set(strcat_r(prefix, "domain", tmp), trim_r(value));
	if ((value = getenv("lease"))) {
		nvram_set(strcat_r(prefix, "lease", tmp), trim_r(value));
	}
	
	if (changed)
	{
		update_resolvconf(0, 0);
		
		if (unit == 0)
			update_wan_status(1);
		
		logmessage("DHCP WAN Client", "%s (%s), new dns: %s", 
			udhcp_state, 
			wan_ifname,
			nvram_safe_get(strcat_r(prefix, "dns", tmp)) );
	}
	
	return 0;
}

static int 
udhcpc_leasefail(char *wan_ifname)
{
	return 0;
}

static int 
udhcpc_noack(char *wan_ifname)
{
	logmessage("DHCP WAN Client", "nak", wan_ifname);
	return 0;
}

int
udhcpc_main(int argc, char **argv)
{
	char *wan_ifname;

	if (argc<2 || !argv[1])
		return EINVAL;

	wan_ifname = safe_getenv("interface");
	strncpy(udhcp_state, argv[1], sizeof(udhcp_state));

	if (!strcmp(argv[1], "deconfig"))
		return udhcpc_deconfig(wan_ifname, 0);
	else if (!strcmp(argv[1], "bound"))
		return udhcpc_bound(wan_ifname);
	else if (!strcmp(argv[1], "renew"))
		return udhcpc_renew(wan_ifname);
	else if (!strcmp(argv[1], "leasefail"))
		return udhcpc_leasefail(wan_ifname);
	else if (!strcmp(argv[1], "nak"))
		return udhcpc_noack(wan_ifname);

	return 0;
}

int
zcip_main(int argc, char **argv)
{
	int ret = 0;
	char *wan_ifname;

	if (argc<2 || !argv[1])
		return EINVAL;

	wan_ifname = safe_getenv("interface");
	strncpy(udhcp_state, argv[1], sizeof(udhcp_state));

	if (!strcmp(argv[1], "deconfig"))
		ret = udhcpc_deconfig(wan_ifname, 1);
	else if (!strcmp(argv[1], "config"))
		ret = zcip_bound(wan_ifname);

	return ret;
}

int start_udhcpc_wan(const char *wan_ifname, int unit, int wait_lease)
{
	char tmp[100], prefix[16];
	char pidfile[32];
	char *wan_hostname;
	int index;
	
	sprintf(pidfile, "/var/run/udhcpc%d.pid", unit);
	
	char *dhcp_argv[] = {
		"/sbin/udhcpc",
		"-i", (char *)wan_ifname,
		"-s", SCRIPT_UDHCPC_WAN,
		"-p", pidfile,
		"-t4",
		"-T4",
		NULL,
		NULL, NULL,	/* -H wan_hostname	*/
		NULL,		/* -O routes		*/
		NULL,		/* -O staticroutes	*/
		NULL,		/* -O msstaticroutes	*/
#if defined (USE_IPV6)
		NULL,		/* -O 6rd		*/
		NULL,		/* -O comcast6rd	*/
#endif
		NULL
	};
	index = 9;		/* first NULL index	*/
	
	if (wait_lease)
		dhcp_argv[index++] = "-b"; /* Background if lease is not obtained (timeout 4*4 sec) */
	else
		dhcp_argv[index++] = "-d"; /* Background after run (new patch for udhcpc) */
	
	/* We have to trust unit */
	snprintf(prefix, sizeof(prefix), "wan%d_", unit);
	
	wan_hostname = nvram_safe_get(strcat_r(prefix, "hostname", tmp));
	if (*wan_hostname) {
		dhcp_argv[index++] = "-H";
		dhcp_argv[index++] = wan_hostname;
	}
	
	if (nvram_match("dr_enable_x", "1")) {
		dhcp_argv[index++] = "-O33";	/* "routes" */
		dhcp_argv[index++] = "-O121";	/* "staticroutes" */
		dhcp_argv[index++] = "-O249";   /* "msstaticroutes" */
	}
	
#if defined (USE_IPV6)
	if (get_ipv6_type() == IPV6_6RD) {
		dhcp_argv[index++] = "-O150";	/* "comcast6rd" */
		dhcp_argv[index++] = "-O212";	/* "6rd" */
	}
#endif
	logmessage("DHCP WAN Client", "starting wan dhcp (%s) ...", wan_ifname);
	
	return _eval(dhcp_argv, NULL, 0, NULL);
}

int start_zcip_wan(const char *wan_ifname)
{
	logmessage("ZeroConf WAN Client", "starting wan zcip (%s) ...", wan_ifname);
	
	return eval("/sbin/zcip", (char*)wan_ifname, SCRIPT_ZCIP_WAN);
}

int renew_udhcpc_wan(int unit)
{
	char pidfile[32];
	
	sprintf(pidfile, "/var/run/udhcpc%d.pid", unit);
	
	return kill_pidfile_s(pidfile, SIGUSR1);
}

int release_udhcpc_wan(int unit)
{
	char pidfile[32];
	
	sprintf(pidfile, "/var/run/udhcpc%d.pid", unit);
	
	return kill_pidfile_s(pidfile, SIGUSR2);
}

int stop_udhcpc_wan(int unit)
{
	char pidfile[32];
	
	sprintf(pidfile, "/var/run/udhcpc%d.pid", unit);
	
	return kill_pidfile_s(pidfile, SIGTERM);
}