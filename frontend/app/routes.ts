import { type RouteConfig, index, route } from "@react-router/dev/routes";

export default [
  index("./routes/home.tsx"),
  route("writing", "./routes/writing.tsx"),
  route("dashboard", "./routes/dashboard.tsx"),
] satisfies RouteConfig;
