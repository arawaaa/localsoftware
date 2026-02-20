import type { Config } from "@react-router/dev/config";

export default {
  // Prerender all routes in the application
  ssr: false,
  appDirectory: "app",
  prerender: ["/", "/writing", "/dashboard"],
} satisfies Config;
