import { BrowserRouter, Routes, Route } from "react-router-dom";
import { AppShell } from "@/components/AppShell";
import { Dashboard } from "@/pages/Dashboard";
import { History } from "@/pages/History";
import { Stats } from "@/pages/Stats";
import { Config } from "@/pages/Config";

export default function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route element={<AppShell />}>
          <Route index element={<Dashboard />} />
          <Route path="historical" element={<History />} />
          {/* /status kept as an alias so old bookmarks keep working. */}
          <Route path="stats" element={<Stats />} />
          <Route path="status" element={<Stats />} />
          <Route path="config" element={<Config />} />
          <Route path="*" element={<Dashboard />} />
        </Route>
      </Routes>
    </BrowserRouter>
  );
}
